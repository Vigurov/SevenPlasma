/*
 * "Network and Internet" pages of the Windows 7 style Control Panel:
 * Network and Sharing Center, Network Connections and the "Set up a connection" wizard.
 * Everything is backed by NetworkManager through nmcli.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7chrome.h"

#include <AeroQt/wizard.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHostInfo>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QWizard>
#include <QWizardPage>

namespace Win7Net
{
enum {
    CenterView = 70,
    ConnectionsView = 71,
    SharingView = 85, // Advanced sharing settings (win7sharing.h)
};

/* ---------------- nmcli ---------------- */

struct Result {
    bool ok = false;
    QString out;
    QString err;
};

inline Result nmcli(const QStringList &args, const QByteArray &input = {}, int timeoutMs = 20000)
{
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8")); // stable, parseable output
    p.setProcessEnvironment(env);
    p.start(QStringLiteral("nmcli"), args);
    if (!p.waitForStarted(3000)) {
        return {false, {}, tr7("nmcli not found")};
    }
    if (!input.isEmpty()) {
        p.write(input);
    }
    p.closeWriteChannel();
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        return {false, {}, tr7("The operation timed out")};
    }
    return {p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0, QString::fromUtf8(p.readAllStandardOutput()),
            QString::fromUtf8(p.readAllStandardError()).trimmed()};
}

/** Split one line of `nmcli -t` output ("a:b\:c" -> {"a", "b:c"}) */
inline QStringList splitTerse(const QString &line)
{
    QStringList fields;
    QString cur;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('\\') && i + 1 < line.size()) {
            cur += line.at(++i);
        } else if (c == QLatin1Char(':')) {
            fields << cur;
            cur.clear();
        } else {
            cur += c;
        }
    }
    fields << cur;
    return fields;
}

inline QList<QStringList> terse(const QStringList &args)
{
    QList<QStringList> rows;
    const QStringList lines = nmcli(args).out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &l : lines) {
        rows << splitTerse(l);
    }
    return rows;
}

/** One value of a connection or device ("nmcli -g") */
inline QString value(const QStringList &args)
{
    return nmcli(args).out.trimmed();
}

inline QString conValue(const QString &uuid, const QString &key, bool secrets = false)
{
    QStringList args{QStringLiteral("-g"), key};
    if (secrets) {
        args.prepend(QStringLiteral("-s"));
    }
    args << QStringLiteral("con") << QStringLiteral("show") << uuid;
    return value(args);
}

inline QString devValue(const QString &iface, const QString &key)
{
    return value({QStringLiteral("-g"), key, QStringLiteral("dev"), QStringLiteral("show"), iface});
}

/** nmcli "-g" escapes ':' in values */
inline QString unescape(QString s)
{
    s.replace(QStringLiteral("\\:"), QStringLiteral(":"));
    return s;
}

inline bool modify(QWidget *parent, const QString &uuid, const QStringList &settings)
{
    if (settings.isEmpty()) {
        return true;
    }
    const Result r = nmcli(QStringList{QStringLiteral("con"), QStringLiteral("modify"), uuid} + settings);
    if (!r.ok) {
        QMessageBox::warning(parent, tr7("Network Connections"), tr7("The connection settings could not be changed.\n\n%1").arg(r.err));
    }
    return r.ok;
}

inline bool askYesNo(QWidget *parent, const QString &title, const QString &text, QMessageBox::Icon icon = QMessageBox::Question)
{
    QMessageBox box(icon, title, text, QMessageBox::NoButton, parent);
    QPushButton *yes = box.addButton(tr7("Yes"), QMessageBox::YesRole);
    box.addButton(tr7("No"), QMessageBox::NoRole);
    box.setDefaultButton(yes);
    box.exec();
    return box.clickedButton() == yes;
}

inline QString userName()
{
    return QString::fromLocal8Bit(qgetenv("USER"));
}

inline bool hasPptp()
{
    return QFile::exists(QStringLiteral("/usr/lib/NetworkManager/VPN/nm-pptp-service.name"));
}

/* ---------------- tiles (what "Network Connections" shows) ---------------- */

struct Tile {
    enum Kind {
        Ethernet,
        Wifi,
        Dialup, // PPPoE
        Vpn,
    } kind = Ethernet;
    QString uuid; // connection profile, may be empty for an adapter
    QString name;
    QString iface;
    QString devState; // for adapters
    bool active = false;
    QString status;
    QString description;
    bool redX = false;
    bool grey = false;
    int signal = 0; // wifi, 0..100

    QString key() const
    {
        return iface.isEmpty() ? uuid : iface;
    }
    bool isAdapter() const
    {
        return kind == Ethernet || kind == Wifi;
    }
};

inline QString productOf(const QString &iface)
{
    static QHash<QString, QString> cache;
    if (!cache.contains(iface)) {
        QString product = devValue(iface, QStringLiteral("GENERAL.PRODUCT"));
        const QString vendor = devValue(iface, QStringLiteral("GENERAL.VENDOR"));
        if (product.isEmpty()) {
            product = iface;
        } else if (!vendor.isEmpty() && !product.contains(vendor.section(QLatin1Char(' '), 0, 0))) {
            product = vendor.section(QLatin1Char(' '), 0, 0) + QLatin1Char(' ') + product;
        }
        cache.insert(iface, product);
    }
    return cache.value(iface);
}

inline QList<Tile> loadTiles()
{
    QList<Tile> tiles;
    // NAME, UUID, TYPE, DEVICE, ACTIVE, TIMESTAMP
    const QList<QStringList> cons = terse({QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("NAME,UUID,TYPE,DEVICE,ACTIVE,TIMESTAMP"),
                                           QStringLiteral("con"), QStringLiteral("show")});
    auto conByUuid = [&cons](const QString &uuid) -> QStringList {
        for (const QStringList &c : cons) {
            if (c.value(1) == uuid) {
                return c;
            }
        }
        return {};
    };
    // DEVICE, TYPE, STATE, CON-UUID
    const QList<QStringList> devs = terse({QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("DEVICE,TYPE,STATE,CON-UUID"), QStringLiteral("dev")});
    for (const QStringList &d : devs) {
        const QString type = d.value(1);
        if (type != QLatin1String("ethernet") && type != QLatin1String("wifi")) {
            continue;
        }
        Tile t;
        t.kind = type == QLatin1String("wifi") ? Tile::Wifi : Tile::Ethernet;
        t.iface = d.value(0);
        t.devState = d.value(2);
        t.uuid = d.value(3);
        t.active = t.devState.startsWith(QLatin1String("connected"));
        t.description = productOf(t.iface);
        const bool off = t.devState == QLatin1String("unmanaged") || (t.kind == Tile::Wifi && t.devState == QLatin1String("unavailable"));
        if (t.kind == Tile::Ethernet) {
            if (t.uuid.isEmpty()) { // not connected: the most recently used wired profile
                qlonglong best = -1;
                for (const QStringList &c : cons) {
                    if (c.value(2) == QLatin1String("802-3-ethernet") && c.value(5).toLongLong() > best) {
                        best = c.value(5).toLongLong();
                        t.uuid = c.value(1);
                    }
                }
            }
            t.name = t.uuid.isEmpty() ? tr7("Local Area Connection") : conByUuid(t.uuid).value(0);
            if (off || t.devState == QLatin1String("disconnected")) {
                t.status = tr7("Off", "firewall state");
                t.grey = true;
            } else if (t.devState == QLatin1String("unavailable")) {
                t.status = tr7("Network cable unplugged");
                t.redX = true;
            } else if (t.active) {
                t.status = tr7("Connected");
            } else {
                t.status = tr7("Identifying...");
            }
        } else {
            t.name = tr7("Wireless Network Connection");
            if (off) {
                t.status = tr7("Off", "firewall state");
                t.grey = true;
            } else if (t.active) {
                t.status = conByUuid(t.uuid).value(0);
                const QList<QStringList> aps = terse({QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("IN-USE,SIGNAL"), QStringLiteral("dev"),
                                                      QStringLiteral("wifi"), QStringLiteral("list"), QStringLiteral("ifname"), t.iface,
                                                      QStringLiteral("--rescan"), QStringLiteral("no")});
                for (const QStringList &ap : aps) {
                    if (ap.value(0) == QLatin1String("*")) {
                        t.signal = ap.value(1).toInt();
                    }
                }
            } else {
                t.status = tr7("Not connected", "network");
                t.redX = true;
            }
        }
        tiles << t;
    }
    for (const QStringList &c : cons) {
        const QString type = c.value(2);
        Tile t;
        if (type == QLatin1String("pppoe")) {
            t.kind = Tile::Dialup;
            t.description = tr7("Broadband Connection (PPPoE)");
        } else if (type == QLatin1String("vpn") || type == QLatin1String("wireguard")) {
            t.kind = Tile::Vpn;
            t.description = type == QLatin1String("wireguard") ? QStringLiteral("VPN (WireGuard)") : tr7("VPN Connection");
        } else {
            continue;
        }
        t.uuid = c.value(1);
        t.name = c.value(0);
        t.active = c.value(4) == QLatin1String("yes");
        t.status = t.active ? tr7("Connected") : tr7("Disabled");
        t.grey = !t.active;
        tiles << t;
    }
    return tiles;
}

inline QImage greyed(const QImage &src)
{
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb c = line[x];
            const int g = qMin(255, qGray(c) * 3 / 4 + 70);
            line[x] = qRgba(g, g, g, qAlpha(c));
        }
    }
    return img;
}

/** 48 px tile icon: computer + adapter badge, greyed when off, red X when not connected */
inline QPixmap tileIcon(const Tile &t, int size = 48)
{
    QImage img(48, 48, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawPixmap(2, 0, QIcon::fromTheme(QStringLiteral("seven-net-computer")).pixmap(32, 32));
    QString badge;
    switch (t.kind) {
    case Tile::Ethernet:
        badge = QStringLiteral("seven-net-plug");
        break;
    case Tile::Wifi:
        badge = QStringLiteral("seven-net-bars%1").arg(t.active ? qBound(1, (t.signal + 19) / 20, 5) : 0);
        break;
    case Tile::Dialup:
        badge = QStringLiteral("network-modem");
        break;
    case Tile::Vpn:
        badge = QStringLiteral("network-vpn");
        break;
    }
    p.drawPixmap(QRect(20, 24, 24, 24), QIcon::fromTheme(badge).pixmap(32, 32));
    p.end();
    if (t.grey) {
        img = greyed(img);
    }
    if (t.redX) {
        QPainter px(&img);
        px.drawPixmap(8, 30, QIcon::fromTheme(QStringLiteral("seven-net-x")).pixmap(16, 16));
    }
    QPixmap pm = QPixmap::fromImage(img);
    return size == 48 ? pm : pm.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

/* ---------------- small UI pieces ---------------- */

inline QLabel *blueTitle(const QString &text, int px = 16)
{
    auto *l = new QLabel(text);
    QFont f = l->font();
    f.setPixelSize(px);
    l->setFont(f);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color: #1e3287;"));
    return l;
}

inline QLabel *plain(const QString &text, bool wrap = true)
{
    auto *l = new QLabel(text);
    l->setWordWrap(wrap);
    return l;
}

inline QPushButton *win7Button(const QString &text, int minWidth = 75)
{
    auto *b = new QPushButton(text);
    b->setMinimumWidth(minWidth);
    return b;
}

/** "Label: [value]" rows used by the status and details dialogs */
inline QLabel *rightAligned(const QString &text)
{
    auto *l = new QLabel(text);
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return l;
}

inline QString prefixToMask(int prefix)
{
    const quint32 mask = prefix <= 0 ? 0 : (0xffffffffu << (32 - qMin(prefix, 32)));
    return QStringLiteral("%1.%2.%3.%4").arg(mask >> 24).arg((mask >> 16) & 255).arg((mask >> 8) & 255).arg(mask & 255);
}

inline int maskToPrefix(const QString &mask)
{
    int bits = 0;
    for (const QString &part : mask.split(QLatin1Char('.'))) {
        bits += qPopulationCount(static_cast<quint32>(part.toUInt() & 255));
    }
    return bits;
}

inline QString formatBytes(qulonglong n)
{
    QString s = QString::number(n);
    for (int i = s.size() - 3; i > 0; i -= 3) {
        s.insert(i, QChar(0x00a0));
    }
    return s;
}

inline QString readSys(const QString &iface, const QString &file)
{
    QFile f(QStringLiteral("/sys/class/net/%1/%2").arg(iface, file));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromLatin1(f.readAll()).trimmed();
}

/** Windows 7 IP address control: one white box, four centred octets with fixed dots between them.
 *  Typing three digits or "." jumps to the next octet, Backspace in an empty octet goes back. */
class IpField : public QFrame
{
public:
    IpField()
    {
        setObjectName(QStringLiteral("ipfield"));
        setFixedSize(125, 21);
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(2, 1, 2, 1);
        h->setSpacing(0);
        static const QRegularExpression octet(QStringLiteral("^(25[0-5]|2[0-4]\\d|1?\\d?\\d)?$"));
        for (int i = 0; i < 4; ++i) {
            auto *e = new QLineEdit;
            e->setFrame(false);
            e->setAlignment(Qt::AlignCenter);
            e->setMaxLength(3);
            e->setValidator(new QRegularExpressionValidator(octet, e));
            e->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0; }"));
            e->installEventFilter(this);
            connect(e, &QLineEdit::textEdited, this, [this, i](const QString &t) {
                if (t.size() == 3 && i < 3) {
                    m_parts[i + 1]->setFocus();
                    m_parts[i + 1]->selectAll();
                }
            });
            m_parts.append(e);
            h->addWidget(e, 1);
            if (i < 3) {
                auto *dot = new QLabel(QStringLiteral("."));
                dot->setAlignment(Qt::AlignBottom | Qt::AlignHCenter);
                h->addWidget(dot);
            }
        }
        updateLook();
    }
    QString text() const
    {
        QStringList p;
        for (QLineEdit *e : m_parts) {
            p << e->text();
        }
        return p.join(QString()).isEmpty() ? QString() : p.join(QLatin1Char('.'));
    }
    void setText(const QString &t)
    {
        const QStringList p = t.trimmed().split(QLatin1Char('.'));
        for (int i = 0; i < 4; ++i) {
            m_parts[i]->setText(t.trimmed().isEmpty() ? QString() : p.value(i));
        }
    }
    /** all four octets filled */
    bool isComplete() const
    {
        for (QLineEdit *e : m_parts) {
            if (e->text().isEmpty()) {
                return false;
            }
        }
        return true;
    }

protected:
    void changeEvent(QEvent *e) override
    {
        if (e->type() == QEvent::EnabledChange) {
            updateLook();
        }
        QFrame::changeEvent(e);
    }
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::KeyPress) {
            const int i = m_parts.indexOf(static_cast<QLineEdit *>(o));
            auto *k = static_cast<QKeyEvent *>(e);
            if ((k->key() == Qt::Key_Period || k->key() == Qt::Key_Space) && i < 3) {
                if (!m_parts[i]->text().isEmpty()) {
                    m_parts[i + 1]->setFocus();
                    m_parts[i + 1]->selectAll();
                }
                return true;
            }
            if (k->key() == Qt::Key_Backspace && i > 0 && m_parts[i]->cursorPosition() == 0 && !m_parts[i]->hasSelectedText()) {
                m_parts[i - 1]->setFocus();
                m_parts[i - 1]->end(false);
                return true;
            }
        }
        return QFrame::eventFilter(o, e);
    }

private:
    void updateLook()
    {
        // Windows 7: white field with a grey-blue border, light grey when disabled
        setStyleSheet(isEnabled() ? QStringLiteral("#ipfield { background: white; border: 1px solid #abadb3; }")
                                  : QStringLiteral("#ipfield { background: #f4f4f4; border: 1px solid #c6c8cc; } QLabel, QLineEdit { color: #a0a0a0; }"));
    }
    QList<QLineEdit *> m_parts;
};

inline IpField *ipEdit()
{
    return new IpField;
}

inline QString connectivityText(const QString &v)
{
    if (v.contains(QLatin1String("full"))) {
        return tr7("Internet");
    }
    if (v.contains(QLatin1String("limited")) || v.contains(QLatin1String("portal"))) {
        return tr7("No Internet access");
    }
    return tr7("No network access");
}

/* ---------------- dialogs ---------------- */

/** Rows as low as in Windows 7 list views (the Aero / Kvantum style makes them ~22 px) */
class CompactRowDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &o, const QModelIndex &i) const override
    {
        return QSize(QStyledItemDelegate::sizeHint(o, i).width(), 17);
    }
};

inline void diagnoseNetwork(QWidget *parent);
inline QSettings &netStore();

/** "Alternate Configuration" tab: used when no DHCP server answers. NetworkManager falls back to an
 *  automatic private (169.254.x.x) address itself; a user-configured alternate address is not supported. */
inline QWidget *alternateConfigTab()
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(10, 10, 10, 8);
    v->setSpacing(4);
    v->addWidget(plain(tr7("If this computer is used on more than one network, enter the alternate IP settings below.")));
    v->addSpacing(6);
    auto *apipa = new QRadioButton(tr7("Automatic private IP address"));
    auto *user = new QRadioButton(tr7("User configured"));
    apipa->setChecked(true);
    user->setEnabled(false);
    v->addWidget(apipa);
    v->addWidget(user);
    auto *box = new QGroupBox;
    auto *g = new QGridLayout(box);
    g->setColumnStretch(0, 1);
    g->setVerticalSpacing(5);
    const QStringList labels = {tr7("IP address:"), tr7("Subnet mask:"), tr7("Default gateway:"), QString(),
                                tr7("Preferred DNS server:"), tr7("Alternate DNS server:"), QString(),
                                tr7("Preferred WINS server:"), tr7("Alternate WINS server:")};
    for (int i = 0; i < labels.size(); ++i) {
        if (labels[i].isEmpty()) {
            g->setRowMinimumHeight(i, 6);
            continue;
        }
        g->addWidget(new QLabel(labels[i]), i, 0);
        g->addWidget(ipEdit(), i, 1);
    }
    box->setEnabled(false);
    v->addWidget(box);
    v->addStretch(1);
    return page;
}

/** "Advanced TCP/IP Settings": what the connection uses now (addresses, gateways, DNS), read only */
inline void advancedTcpIp(QWidget *parent, const QString &uuid)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr7("Advanced TCP/IP Settings"));
    dlg.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowContextHelpButtonHint | Qt::WindowCloseButtonHint);
    dlg.setFixedSize(400, 470);
    auto *v = new QVBoxLayout(&dlg);
    auto *tabs = new QTabWidget;
    auto listBox = [](const QString &title, const QStringList &cols, const QList<QStringList> &rows) {
        auto *box = new QGroupBox(title);
        auto *bv = new QVBoxLayout(box);
        auto *t = new QTreeWidget;
        t->setRootIsDecorated(false);
        t->setHeaderLabels(cols);
        t->setHeaderHidden(cols.size() < 2);
        t->setItemDelegate(new CompactRowDelegate(t));
        for (const QStringList &r : rows) {
            new QTreeWidgetItem(t, r);
        }
        t->setMinimumHeight(70);
        bv->addWidget(t);
        auto *h = new QHBoxLayout;
        h->addStretch(1);
        for (const char *b : {"Add...", "Edit...", "Delete"}) {
            auto *btn = win7Button(tr7(b));
            btn->setEnabled(false);
            h->addWidget(btn);
        }
        h->addStretch(1);
        bv->addLayout(h);
        return box;
    };
    const QString iface = conValue(uuid, QStringLiteral("GENERAL.IP-IFACE"));
    QMap<QString, QStringList> info;
    for (const QStringList &r : terse({QStringLiteral("-t"), QStringLiteral("dev"), QStringLiteral("show"), iface})) {
        info[r.value(0).section(QLatin1Char('['), 0, 0)] << r.mid(1).join(QLatin1Char(':'));
    }
    const bool dhcp = conValue(uuid, QStringLiteral("ipv4.method")) == QLatin1String("auto");
    QList<QStringList> addrs, gws, dnss;
    if (dhcp) {
        addrs << QStringList{tr7("DHCP Enabled"), QString()};
    } else {
        for (const QString &a : info.value(QStringLiteral("IP4.ADDRESS"))) {
            addrs << QStringList{a.section(QLatin1Char('/'), 0, 0), prefixToMask(a.section(QLatin1Char('/'), 1, 1).toInt())};
        }
    }
    const QString gw = info.value(QStringLiteral("IP4.GATEWAY")).value(0);
    if (!gw.isEmpty() && gw != QLatin1String("--")) {
        gws << QStringList{gw, tr7("Automatic")};
    }
    for (const QString &d : info.value(QStringLiteral("IP4.DNS"))) {
        dnss << QStringList{d};
    }
    auto *ipPage = new QWidget;
    auto *iv = new QVBoxLayout(ipPage);
    iv->addWidget(listBox(tr7("IP addresses"), {tr7("IP address"), tr7("Subnet mask")}, addrs));
    iv->addWidget(listBox(tr7("Default gateways:"), {tr7("Gateway"), tr7("Metric")}, gws));
    tabs->addTab(ipPage, tr7("IP Settings"));
    auto *dnsPage = new QWidget;
    auto *dv = new QVBoxLayout(dnsPage);
    dv->addWidget(listBox(tr7("DNS server addresses, in order of use:"), {QStringLiteral("DNS")}, dnss));
    dv->addStretch(1);
    tabs->addTab(dnsPage, QStringLiteral("DNS"));
    auto *winsPage = new QWidget;
    auto *wv = new QVBoxLayout(winsPage);
    wv->addWidget(listBox(tr7("WINS addresses, in order of use:"), {QStringLiteral("WINS")}, {}));
    wv->addStretch(1);
    tabs->addTab(winsPage, QStringLiteral("WINS"));
    v->addWidget(tabs);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    auto *ok = win7Button(QStringLiteral("OK"));
    auto *cancel = win7Button(tr7("Cancel"));
    row->addWidget(ok);
    row->addWidget(cancel);
    v->addLayout(row);
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    dlg.exec();
}

/** "Internet Protocol Version 4 (TCP/IPv4) Properties": collects the nmcli settings, applied by the caller */
inline bool editIpv4(QWidget *parent, const QString &uuid, QStringList *settings)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr7("Internet Protocol Version 4 (TCP/IPv4) Properties"));
    // Windows 7: "?" and close only, fixed size
    dlg.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowContextHelpButtonHint | Qt::WindowCloseButtonHint);
    auto *v = new QVBoxLayout(&dlg);
    v->setContentsMargins(8, 8, 8, 8);
    auto *tabs = new QTabWidget;
    auto *page = new QWidget;
    auto *pv = new QVBoxLayout(page);
    pv->setContentsMargins(10, 10, 10, 8);
    pv->setSpacing(4);
    pv->addWidget(plain(tr7("You can get IP settings assigned automatically if your network supports this capability. Otherwise, you need to ask your network administrator for the appropriate IP settings.")));
    pv->addSpacing(6);

    const QString method = conValue(uuid, QStringLiteral("ipv4.method"));
    const QString address = conValue(uuid, QStringLiteral("ipv4.addresses")).section(QLatin1Char(','), 0, 0).trimmed();
    const QString gateway = conValue(uuid, QStringLiteral("ipv4.gateway"));
    const QStringList dns = conValue(uuid, QStringLiteral("ipv4.dns")).split(QLatin1Char(','), Qt::SkipEmptyParts);
    const bool ignoreAutoDns = conValue(uuid, QStringLiteral("ipv4.ignore-auto-dns")) == QLatin1String("yes");

    auto *autoIp = new QRadioButton(tr7("Obtain an IP address automatically"));
    auto *manualIp = new QRadioButton(tr7("Use the following IP address:"));
    auto *ip = ipEdit();
    auto *mask = ipEdit();
    auto *gw = ipEdit();
    ip->setText(address.section(QLatin1Char('/'), 0, 0));
    if (!address.isEmpty()) {
        mask->setText(prefixToMask(address.section(QLatin1Char('/'), 1, 1).toInt()));
    }
    gw->setText(gateway == QLatin1String("--") ? QString() : gateway);
    (method == QLatin1String("manual") ? manualIp : autoIp)->setChecked(true);

    auto *ipBox = new QGroupBox;
    ipBox->setFlat(false);
    auto *ipForm = new QGridLayout(ipBox);
    ipForm->setColumnStretch(0, 1);
    ipForm->setVerticalSpacing(5);
    ipForm->addWidget(new QLabel(tr7("IP address:")), 0, 0);
    ipForm->addWidget(ip, 0, 1);
    ipForm->addWidget(new QLabel(tr7("Subnet mask:")), 1, 0);
    ipForm->addWidget(mask, 1, 1);
    ipForm->addWidget(new QLabel(tr7("Default gateway:")), 2, 0);
    ipForm->addWidget(gw, 2, 1);
    pv->addWidget(autoIp);
    pv->addWidget(manualIp);
    pv->addWidget(ipBox);

    auto *autoDns = new QRadioButton(tr7("Obtain DNS server address automatically"));
    auto *manualDns = new QRadioButton(tr7("Use the following DNS server addresses:"));
    auto *dns1 = ipEdit();
    auto *dns2 = ipEdit();
    dns1->setText(dns.value(0).trimmed());
    dns2->setText(dns.value(1).trimmed());
    ((ignoreAutoDns || method == QLatin1String("manual")) && !dns.isEmpty() ? manualDns : autoDns)->setChecked(true);
    auto *dnsBox = new QGroupBox;
    auto *dnsForm = new QGridLayout(dnsBox);
    dnsForm->setColumnStretch(0, 1);
    dnsForm->setVerticalSpacing(5);
    dnsForm->addWidget(new QLabel(tr7("Preferred DNS server:")), 0, 0);
    dnsForm->addWidget(dns1, 0, 1);
    dnsForm->addWidget(new QLabel(tr7("Alternate DNS server:")), 1, 0);
    dnsForm->addWidget(dns2, 1, 1);
    pv->addSpacing(6);
    pv->addWidget(autoDns);
    pv->addWidget(manualDns);
    pv->addWidget(dnsBox);
    pv->addSpacing(6);
    auto *bottom = new QHBoxLayout;
    auto *validate = new QCheckBox(tr7("Validate settings upon exit"));
    validate->setChecked(netStore().value(QStringLiteral("ValidateOnExit"), false).toBool());
    bottom->addWidget(validate);
    bottom->addStretch(1);
    auto *advanced = win7Button(tr7("Advanced..."));
    bottom->addWidget(advanced);
    pv->addLayout(bottom);
    QObject::connect(advanced, &QPushButton::clicked, &dlg, [&dlg, uuid] {
        advancedTcpIp(&dlg, uuid);
    });

    auto sync = [=] {
        ipBox->setEnabled(manualIp->isChecked());
        if (manualIp->isChecked()) {
            manualDns->setChecked(true);
        }
        autoDns->setEnabled(!manualIp->isChecked());
        dnsBox->setEnabled(manualDns->isChecked());
    };
    for (QRadioButton *r : {autoIp, manualIp, autoDns, manualDns}) {
        QObject::connect(r, &QRadioButton::toggled, &dlg, sync);
    }
    sync();

    tabs->addTab(page, tr7("General"));
    tabs->addTab(alternateConfigTab(), tr7("Alternate Configuration"));
    QObject::connect(manualIp, &QRadioButton::toggled, tabs, [tabs](bool manual) {
        tabs->setTabVisible(1, !manual); // Windows 7 hides it for a static address
    });
    tabs->setTabVisible(1, !manualIp->isChecked());
    v->addWidget(tabs);
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(win7Button(QStringLiteral("OK")), QDialogButtonBox::AcceptRole);
    buttons->addButton(win7Button(tr7("Cancel")), QDialogButtonBox::RejectRole);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, [&] {
        if (manualIp->isChecked() && (!ip->isComplete() || !mask->isComplete())) {
            QMessageBox::warning(&dlg, tr7("Internet Protocol Version 4 (TCP/IPv4)"), tr7("Type an IP address and a subnet mask."));
            return;
        }
        dlg.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(buttons);
    // fixed size like Windows 7 (416 x 462 with the frame): KWin then shows only "?" and close
    dlg.setFixedSize(410, v->totalHeightForWidth(410));
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    netStore().setValue(QStringLiteral("ValidateOnExit"), validate->isChecked());
    if (validate->isChecked()) {
        QTimer::singleShot(4000, [] { diagnoseNetwork(nullptr); }); // after the new settings are applied
    }
    if (manualIp->isChecked()) {
        *settings << QStringLiteral("ipv4.method") << QStringLiteral("manual") << QStringLiteral("ipv4.addresses")
                  << QStringLiteral("%1/%2").arg(ip->text()).arg(maskToPrefix(mask->text())) << QStringLiteral("ipv4.gateway") << gw->text();
    } else {
        *settings << QStringLiteral("ipv4.method") << QStringLiteral("auto") << QStringLiteral("ipv4.addresses") << QString()
                  << QStringLiteral("ipv4.gateway") << QString();
    }
    if (manualDns->isChecked()) {
        QStringList servers;
        for (IpField *e : {dns1, dns2}) {
            if (e->isComplete()) {
                servers << e->text();
            }
        }
        *settings << QStringLiteral("ipv4.ignore-auto-dns") << QStringLiteral("yes") << QStringLiteral("ipv4.dns") << servers.join(QLatin1Char(','));
    } else {
        *settings << QStringLiteral("ipv4.ignore-auto-dns") << QStringLiteral("no") << QStringLiteral("ipv4.dns") << QString();
    }
    return true;
}

/** "Networking" tab: the protocols this connection uses */
inline QWidget *networkingTab(QWidget *dlg, const QString &uuid, const QString &through, QStringList *settings)
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(10, 10, 10, 10);
    v->setSpacing(4);
    if (!through.isEmpty()) {
        v->addWidget(new QLabel(tr7("Connect using:")));
        // the adapter in a sunken box with its icon, "Configure..." under it on the right
        auto *box = new QFrame;
        box->setFrameShape(QFrame::StyledPanel);
        box->setFrameShadow(QFrame::Sunken);
        box->setObjectName(QStringLiteral("adapterbox"));
        box->setStyleSheet(QStringLiteral("#adapterbox { background: white; border: 1px solid #abadb3; }"));
        auto *bh = new QHBoxLayout(box);
        bh->setContentsMargins(6, 3, 6, 3);
        auto *ai = new QLabel;
        ai->setPixmap(QIcon::fromTheme(QStringLiteral("seven-net-comp-client")).pixmap(16, 16));
        bh->addWidget(ai);
        auto *an = new QLabel(through);
        an->setMinimumWidth(10);
        bh->addWidget(an, 1);
        v->addWidget(box);
        auto *cfgRow = new QHBoxLayout;
        cfgRow->addStretch(1);
        auto *cfg = win7Button(tr7("Configure..."));
        cfgRow->addWidget(cfg);
        QObject::connect(cfg, &QPushButton::clicked, page, [] {
            QProcess::startDetached(QDir::homePath() + QStringLiteral("/.local/bin/seven-devmgr"), {});
        });
        v->addLayout(cfgRow);
    }
    v->addWidget(new QLabel(tr7("This connection uses the following items:")));
    auto *list = new QTreeWidget;
    list->setHeaderHidden(true);
    list->setRootIsDecorated(false);
    list->setUniformRowHeights(true);
    list->setItemDelegate(new CompactRowDelegate(list));
    list->setMinimumHeight(122);
    const QString v4 = conValue(uuid, QStringLiteral("ipv4.method"));
    const QString v6 = conValue(uuid, QStringLiteral("ipv6.method"));
    const bool smbd = QProcess::execute(QStringLiteral("systemctl"), {QStringLiteral("is-active"), QStringLiteral("--quiet"), QStringLiteral("smbd")}) == 0;
    // Windows 7's list; only the two protocols can be switched here, the rest shows what Linux provides
    struct Comp {
        QString name, icon, desc;
        bool on;
    };
    const QList<Comp> fixed1 = {
        {tr7("Client for Microsoft Networks"), QStringLiteral("seven-net-comp-client"),
         tr7("Allows your computer to access resources on a Microsoft network."), true},
        {tr7("QoS Packet Scheduler"), QStringLiteral("seven-net-comp-client"),
         tr7("Provides network traffic control, including rate-of-flow and prioritization services."), true},
        {tr7("File and Printer Sharing for Microsoft Networks"), QStringLiteral("seven-net-comp-client"),
         tr7("Allows other computers to access resources on your computer using a Microsoft network."), smbd},
    };
    const QList<Comp> fixed2 = {
        {tr7("Link-Layer Topology Discovery Mapper I/O Driver"), QStringLiteral("seven-net-comp-protocol"),
         tr7("Used to discover and locate other PCs, devices, and network infrastructure components on the network. Also used to determine network bandwidth."), true},
        {tr7("Link-Layer Topology Discovery Responder"), QStringLiteral("seven-net-comp-protocol"),
         tr7("Allows this PC to be discovered and located on the network."), true},
    };
    QHash<QTreeWidgetItem *, QString> descs;
    auto addFixed = [&](const Comp &c) {
        auto *it = new QTreeWidgetItem(list, {c.name});
        it->setIcon(0, QIcon::fromTheme(c.icon));
        it->setCheckState(0, c.on ? Qt::Checked : Qt::Unchecked);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable); // shown, not switched from here
        descs.insert(it, c.desc);
    };
    for (const Comp &c : fixed1) {
        addFixed(c);
    }
    auto *ipv6 = new QTreeWidgetItem(list, {tr7("Internet Protocol Version 6 (TCP/IPv6)")});
    ipv6->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-net-comp-protocol")));
    ipv6->setCheckState(0, v6 == QLatin1String("disabled") || v6 == QLatin1String("ignore") ? Qt::Unchecked : Qt::Checked);
    descs.insert(ipv6, tr7("TCP/IP version 6. The latest version of the internet protocol that provides communication across diverse interconnected networks."));
    auto *ipv4 = new QTreeWidgetItem(list, {tr7("Internet Protocol Version 4 (TCP/IPv4)")});
    ipv4->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-net-comp-protocol")));
    ipv4->setCheckState(0, v4 == QLatin1String("disabled") ? Qt::Unchecked : Qt::Checked);
    descs.insert(ipv4, tr7("Transmission Control Protocol/Internet Protocol. The default wide area network protocol that provides communication across diverse interconnected networks."));
    for (const Comp &c : fixed2) {
        addFixed(c);
    }
    v->addWidget(list);
    auto *row = new QHBoxLayout;
    auto *install = win7Button(tr7("Install..."));
    auto *remove = win7Button(tr7("Delete"));
    install->setEnabled(false); // nothing to add or remove on Linux from here
    remove->setEnabled(false);
    auto *props = win7Button(tr7("Properties"));
    row->addWidget(install);
    row->addWidget(remove);
    row->addWidget(props);
    v->addLayout(row);
    auto *descBox = new QGroupBox(tr7("Description"));
    auto *dv = new QVBoxLayout(descBox);
    auto *desc = plain(QString());
    desc->setMinimumHeight(48);
    desc->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    dv->addWidget(desc);
    v->addWidget(descBox);
    v->addStretch(1);

    auto describe = [=] {
        QTreeWidgetItem *cur = list->currentItem();
        props->setEnabled((cur == ipv4 && ipv4->checkState(0) == Qt::Checked));
        desc->setText(descs.value(cur));
    };
    QObject::connect(list, &QTreeWidget::currentItemChanged, page, describe);
    QObject::connect(list, &QTreeWidget::itemChanged, page, [=](QTreeWidgetItem *item) {
        describe();
        const bool on = item->checkState(0) == Qt::Checked;
        if (item == ipv4) {
            *settings << QStringLiteral("ipv4.method") << (on ? QStringLiteral("auto") : QStringLiteral("disabled"));
        } else if (item == ipv6) {
            *settings << QStringLiteral("ipv6.method") << (on ? QStringLiteral("auto") : QStringLiteral("disabled"));
        }
    });
    auto openV4 = [=] {
        if (list->currentItem() == ipv4 && ipv4->checkState(0) == Qt::Checked) {
            editIpv4(dlg, uuid, settings);
        }
    };
    QObject::connect(props, &QPushButton::clicked, page, openV4);
    QObject::connect(list, &QTreeWidget::itemDoubleClicked, page, openV4);
    list->setCurrentItem(list->topLevelItem(0));
    describe();
    return page;
}

inline QDialogButtonBox *okCancel(QDialog *dlg)
{
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(win7Button(QStringLiteral("OK")), QDialogButtonBox::AcceptRole);
    buttons->addButton(win7Button(tr7("Cancel")), QDialogButtonBox::RejectRole);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    return buttons;
}

inline QSettings &netStore()
{
    static QSettings s(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/seven-netrc"), QSettings::IniFormat);
    return s;
}

inline void diagnoseNetwork(QWidget *parent);

/** Properties of a connection. Returns true when something was changed. */
inline bool showProperties(QWidget *parent, const Tile &t)
{
    if (t.uuid.isEmpty()) {
        return false;
    }
    QDialog dlg(parent);
    dlg.setWindowTitle(tr7("%1 Properties").arg(t.name));
    dlg.setMinimumWidth(380);
    auto *v = new QVBoxLayout(&dlg);
    auto *tabs = new QTabWidget;
    QStringList settings;

    // VPN connections are edited by the KDE editor, they have too many plugin specific settings
    if (t.kind == Tile::Vpn) {
        launch(QStringLiteral("/usr/bin/kcmshell6"), {QStringLiteral("kcm_networkmanagement")});
        return false;
    }

    QLineEdit *service = nullptr;
    QCheckBox *showProgress = nullptr, *askCreds = nullptr, *redial = nullptr;
    QSpinBox *retries = nullptr;
    QComboBox *encryption = nullptr;
    QList<QPair<QCheckBox *, QString>> protocols;
    bool lcp = false, compression = false;
    const QString store = QStringLiteral("dialup-%1/").arg(t.uuid);

    if (t.kind == Tile::Dialup) {
        // General
        auto *general = new QWidget;
        auto *gv = new QVBoxLayout(general);
        gv->addWidget(plain(tr7("Service name:")));
        service = new QLineEdit(conValue(t.uuid, QStringLiteral("pppoe.service")));
        gv->addWidget(service);
        gv->addStretch(1);
        tabs->addTab(general, tr7("General"));

        // Options
        auto *options = new QWidget;
        auto *ov = new QVBoxLayout(options);
        auto *dialBox = new QGroupBox(tr7("Dialing options"));
        auto *dbv = new QVBoxLayout(dialBox);
        showProgress = new QCheckBox(tr7("Display progress while connecting"));
        askCreds = new QCheckBox(tr7("Prompt for name, password, certificate, etc."));
        showProgress->setChecked(netStore().value(store + QStringLiteral("ShowProgress"), true).toBool());
        askCreds->setChecked(netStore().value(store + QStringLiteral("AskCredentials"), true).toBool());
        dbv->addWidget(showProgress);
        dbv->addWidget(askCreds);
        ov->addWidget(dialBox);
        auto *redialBox = new QGroupBox(tr7("Redialing options"));
        auto *rf = new QFormLayout(redialBox);
        retries = new QSpinBox;
        retries->setRange(1, 999);
        const int r = conValue(t.uuid, QStringLiteral("connection.autoconnect-retries")).toInt();
        retries->setValue(r <= 0 ? 3 : r);
        retries->setFixedWidth(100);
        rf->addRow(tr7("Redial attempts:"), retries);
        redial = new QCheckBox(tr7("Redial if line is dropped"));
        redial->setChecked(conValue(t.uuid, QStringLiteral("connection.autoconnect")) == QLatin1String("yes"));
        rf->addRow(redial);
        ov->addWidget(redialBox);
        auto *pppRow = new QHBoxLayout;
        auto *pppButton = win7Button(tr7("PPP Settings..."), 110);
        pppRow->addWidget(pppButton);
        pppRow->addStretch(1);
        ov->addLayout(pppRow);
        ov->addStretch(1);
        tabs->addTab(options, tr7("Options"));

        lcp = conValue(t.uuid, QStringLiteral("ppp.lcp-echo-interval")).toInt() > 0;
        compression = conValue(t.uuid, QStringLiteral("ppp.nobsdcomp")) != QLatin1String("yes") || conValue(t.uuid, QStringLiteral("ppp.nodeflate")) != QLatin1String("yes");
        QObject::connect(pppButton, &QPushButton::clicked, &dlg, [&dlg, &settings, &lcp, &compression] {
            QDialog ppp(&dlg);
            ppp.setWindowTitle(tr7("PPP Settings"));
            auto *pv = new QVBoxLayout(&ppp);
            auto *lcpBox = new QCheckBox(tr7("Enable LCP extensions"));
            auto *compBox = new QCheckBox(tr7("Enable software compression"));
            lcpBox->setChecked(lcp);
            compBox->setChecked(compression);
            pv->addWidget(lcpBox);
            pv->addWidget(compBox);
            pv->addSpacing(8);
            pv->addWidget(okCancel(&ppp));
            if (ppp.exec() == QDialog::Accepted) {
                lcp = lcpBox->isChecked();
                compression = compBox->isChecked();
                settings << QStringLiteral("ppp.lcp-echo-interval") << (lcp ? QStringLiteral("30") : QStringLiteral("0")) << QStringLiteral("ppp.lcp-echo-failure")
                         << (lcp ? QStringLiteral("5") : QStringLiteral("0")) << QStringLiteral("ppp.nobsdcomp") << (compression ? QStringLiteral("no") : QStringLiteral("yes"))
                         << QStringLiteral("ppp.nodeflate") << (compression ? QStringLiteral("no") : QStringLiteral("yes"));
            }
        });

        // Security
        auto *security = new QWidget;
        auto *sv = new QVBoxLayout(security);
        sv->addWidget(plain(tr7("Data encryption:")));
        encryption = new QComboBox;
        encryption->addItem(tr7("Optional encryption (connect even if no encryption)"));
        encryption->addItem(tr7("Require encryption (disconnect if server declines)"));
        encryption->setCurrentIndex(conValue(t.uuid, QStringLiteral("ppp.require-mppe")) == QLatin1String("yes") ? 1 : 0);
        sv->addWidget(encryption);
        auto *authBox = new QGroupBox(tr7("Allow these protocols"));
        auto *av = new QVBoxLayout(authBox);
        const QList<QPair<QString, QString>> list{
            {tr7("Unencrypted password (PAP)"), QStringLiteral("ppp.refuse-pap")},
            {tr7("Challenge Handshake Authentication Protocol (CHAP)"), QStringLiteral("ppp.refuse-chap")},
            {tr7("Microsoft CHAP (MS-CHAP)"), QStringLiteral("ppp.refuse-mschap")},
            {tr7("Microsoft CHAP Version 2 (MS-CHAP v2)"), QStringLiteral("ppp.refuse-mschapv2")},
            {tr7("Extensible Authentication Protocol (EAP)"), QStringLiteral("ppp.refuse-eap")},
        };
        for (const auto &[text, key] : list) {
            auto *cb = new QCheckBox(text);
            cb->setChecked(conValue(t.uuid, key) != QLatin1String("yes"));
            av->addWidget(cb);
            protocols.append({cb, key});
        }
        sv->addWidget(authBox);
        sv->addStretch(1);
        tabs->addTab(security, tr7("Security"));
    }
    tabs->addTab(networkingTab(&dlg, t.uuid, t.kind == Tile::Dialup ? QString() : t.description, &settings), tr7("Network"));
    v->addWidget(tabs);
    v->addWidget(okCancel(&dlg));
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    if (t.kind == Tile::Dialup) {
        settings << QStringLiteral("pppoe.service") << service->text() << QStringLiteral("connection.autoconnect") << (redial->isChecked() ? QStringLiteral("yes") : QStringLiteral("no"))
                 << QStringLiteral("connection.autoconnect-retries") << QString::number(retries->value()) << QStringLiteral("ppp.require-mppe")
                 << (encryption->currentIndex() == 1 ? QStringLiteral("yes") : QStringLiteral("no"));
        for (const auto &[cb, key] : std::as_const(protocols)) {
            settings << key << (cb->isChecked() ? QStringLiteral("no") : QStringLiteral("yes"));
        }
        netStore().setValue(store + QStringLiteral("ShowProgress"), showProgress->isChecked());
        netStore().setValue(store + QStringLiteral("AskCredentials"), askCreds->isChecked());
        netStore().sync();
    }
    if (!modify(parent, t.uuid, settings)) {
        return false;
    }
    if (t.active && t.isAdapter()) { // apply right away, like Windows
        nmcli({QStringLiteral("dev"), QStringLiteral("reapply"), t.iface});
    }
    return true;
}

/** Runs `nmcli con up` with a small "Connecting to ..." window */
inline bool bringUp(QWidget *parent, const QString &uuid, const QString &name, const QByteArray &secrets = {}, bool showProgress = true)
{
    QDialog progress(parent);
    progress.setWindowTitle(tr7("Connect %1").arg(name));
    auto *v = new QVBoxLayout(&progress);
    auto *row = new QHBoxLayout;
    auto *ic = new QLabel;
    ic->setPixmap(QIcon::fromTheme(QStringLiteral("seven-net-computers")).pixmap(32, 32));
    row->addWidget(ic);
    row->addWidget(plain(tr7("Connecting to %1...").arg(name)), 1);
    v->addLayout(row);
    auto *cancelRow = new QHBoxLayout;
    cancelRow->addStretch(1);
    auto *cancel = win7Button(tr7("Cancel"));
    cancelRow->addWidget(cancel);
    v->addLayout(cancelRow);
    progress.setMinimumWidth(340);

    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    proc.setProcessEnvironment(env);
    QStringList args{QStringLiteral("con"), QStringLiteral("up"), uuid};
    if (!secrets.isEmpty()) {
        args << QStringLiteral("passwd-file") << QStringLiteral("/dev/stdin");
    }
    bool ok = false;
    QString err;
    QObject::connect(&proc, &QProcess::finished, &progress, [&](int code, QProcess::ExitStatus st) {
        ok = st == QProcess::NormalExit && code == 0;
        err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        progress.accept();
    });
    QObject::connect(cancel, &QPushButton::clicked, &progress, [&] {
        proc.kill();
        nmcli({QStringLiteral("con"), QStringLiteral("down"), uuid});
        err.clear();
        progress.reject();
    });
    proc.start(QStringLiteral("nmcli"), args);
    if (!secrets.isEmpty()) {
        proc.write(secrets);
    }
    proc.closeWriteChannel();
    if (showProgress) {
        progress.exec();
    } else {
        proc.waitForFinished(90000);
    }
    if (!ok && !err.isEmpty()) {
        err.remove(QRegularExpression(QStringLiteral("^Error: ")));
        QMessageBox::warning(parent, tr7("Connection error"), tr7("Could not connect to \"%1\".\n\n%2").arg(name, err));
    }
    return ok;
}

/** Banner of the "Connect <name>" window: laptop — globe — computer */
inline QPixmap connectBanner(int w)
{
    QPixmap pm(w, 110);
    pm.fill(Qt::white);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const int cy = 55;
    QPen line(QColor(0x5f, 0xb3, 0x3a), 4);
    p.setPen(line);
    p.drawLine(w / 2 - 110, cy + 8, w / 2 + 110, cy + 8);
    p.drawPixmap(w / 2 - 150, cy - 40, QIcon::fromTheme(QStringLiteral("computer-laptop"), QIcon::fromTheme(QStringLiteral("computer"))).pixmap(80, 80));
    p.drawPixmap(w / 2 - 44, cy - 46, QIcon::fromTheme(QStringLiteral("seven-net-internet")).pixmap(88, 88));
    p.drawPixmap(w / 2 + 64, cy - 44, QIcon::fromTheme(QStringLiteral("seven-net-computer")).pixmap(88, 88));
    return pm;
}

/** "Connect <name>" window of a PPPoE or VPN connection (Windows 7 rasdial dialog) */
inline void connectDialog(QWidget *parent, const Tile &t, const std::function<void()> &changed)
{
    const QString store = QStringLiteral("dialup-%1/").arg(t.uuid);
    const bool vpn = t.kind == Tile::Vpn;
    if (!vpn && !netStore().value(store + QStringLiteral("AskCredentials"), true).toBool()) {
        bringUp(parent, t.uuid, t.name, {}, netStore().value(store + QStringLiteral("ShowProgress"), true).toBool());
        changed();
        return;
    }
    QDialog dlg(parent);
    dlg.setWindowTitle(tr7("Connection: %1").arg(t.name));
    dlg.setWindowIcon(QIcon(tileIcon(t, 32)));
    auto *v = new QVBoxLayout(&dlg);
    auto *banner = new QLabel;
    banner->setPixmap(connectBanner(340));
    banner->setFrameShape(QFrame::StyledPanel);
    v->addWidget(banner);

    QString user;
    QString vpnData;
    if (vpn) {
        vpnData = unescape(conValue(t.uuid, QStringLiteral("vpn.data")));
        static const QRegularExpression userRe(QStringLiteral("(?:^|,\\s*)user\\s*=\\s*([^,]*)"));
        user = userRe.match(vpnData).captured(1).trimmed();
    } else {
        user = conValue(t.uuid, QStringLiteral("pppoe.username"));
    }
    const QString savedPassword = vpn ? QString() : conValue(t.uuid, QStringLiteral("pppoe.password"), true);
    const bool everyone = conValue(t.uuid, QStringLiteral("connection.permissions")).isEmpty();

    auto *form = new QFormLayout;
    auto *userEdit = new QLineEdit(user);
    auto *passEdit = new QLineEdit;
    passEdit->setEchoMode(QLineEdit::Password);
    if (!savedPassword.isEmpty()) {
        passEdit->setPlaceholderText(tr7("[To change the saved password, click here]"));
    }
    form->addRow(tr7("Username:"), userEdit);
    form->addRow(tr7("Password:"), passEdit);
    v->addLayout(form);
    auto *save = new QCheckBox(tr7("Save this user name and password for the following users:"));
    save->setChecked(vpn ? true : !savedPassword.isEmpty() || netStore().value(store + QStringLiteral("Save"), true).toBool());
    auto *onlyMe = new QRadioButton(tr7("Me only"));
    auto *anyone = new QRadioButton(tr7("Anyone who uses this computer"));
    (everyone ? anyone : onlyMe)->setChecked(true);
    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QStringLiteral("color: #d5dfe5;"));
    v->addWidget(line);
    v->addWidget(save);
    auto *radios = new QVBoxLayout;
    radios->setContentsMargins(20, 0, 0, 0);
    radios->addWidget(onlyMe);
    auto *anyoneRow = new QHBoxLayout;
    auto *shield = new QLabel;
    shield->setPixmap(QIcon::fromTheme(QStringLiteral("seven-cp-shield")).pixmap(16, 16));
    anyoneRow->addWidget(shield);
    anyoneRow->addWidget(anyone, 1);
    anyoneRow->setContentsMargins(0, 0, 0, 0);
    radios->addLayout(anyoneRow);
    v->addLayout(radios);
    QObject::connect(save, &QCheckBox::toggled, &dlg, [=](bool on) {
        onlyMe->setEnabled(on);
        anyone->setEnabled(on);
    });
    onlyMe->setEnabled(save->isChecked());
    anyone->setEnabled(save->isChecked());

    auto *buttons = new QHBoxLayout;
    auto *connectButton = win7Button(tr7("Connections"), 90);
    auto *cancel = win7Button(tr7("Cancel"));
    auto *props = win7Button(tr7("Properties"));
    connectButton->setDefault(true);
    buttons->addWidget(connectButton);
    buttons->addWidget(cancel);
    buttons->addWidget(props);
    v->addLayout(buttons);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(props, &QPushButton::clicked, &dlg, [&dlg, t, changed] {
        if (showProperties(&dlg, t)) {
            changed();
        }
    });
    QObject::connect(connectButton, &QPushButton::clicked, &dlg, [&] {
        const QString password = passEdit->text();
        QStringList settings;
        QByteArray secrets;
        if (vpn) {
            QString data = vpnData;
            static const QRegularExpression userRe(QStringLiteral("(,\\s*|^)user\\s*=\\s*[^,]*"));
            data.remove(userRe);
            if (data.startsWith(QLatin1Char(','))) {
                data.remove(0, 1);
            }
            data = data.trimmed();
            data += (data.isEmpty() ? QString() : QStringLiteral(", ")) + QStringLiteral("user = ") + userEdit->text();
            settings << QStringLiteral("vpn.data") << data;
            if (!password.isEmpty()) {
                secrets = QStringLiteral("vpn.secrets.password:%1\n").arg(password).toUtf8();
                if (save->isChecked()) {
                    settings << QStringLiteral("vpn.secrets") << QStringLiteral("password=%1").arg(password);
                }
            }
        } else {
            settings << QStringLiteral("pppoe.username") << userEdit->text();
            if (save->isChecked()) {
                if (!password.isEmpty()) {
                    settings << QStringLiteral("pppoe.password") << password;
                }
                settings << QStringLiteral("pppoe.password-flags") << QStringLiteral("0");
            } else {
                settings << QStringLiteral("pppoe.password-flags") << QStringLiteral("2");
                secrets = QStringLiteral("pppoe.password:%1\n").arg(password.isEmpty() ? savedPassword : password).toUtf8();
            }
            netStore().setValue(store + QStringLiteral("Save"), save->isChecked());
            netStore().sync();
        }
        if (save->isChecked()) {
            settings << QStringLiteral("connection.permissions") << (anyone->isChecked() ? QString() : QStringLiteral("user:%1").arg(userName()));
        }
        if (!modify(&dlg, t.uuid, settings)) {
            return;
        }
        dlg.hide();
        const bool ok = bringUp(parent, t.uuid, t.name, secrets, vpn || netStore().value(store + QStringLiteral("ShowProgress"), true).toBool());
        changed();
        if (ok) {
            dlg.accept();
        } else {
            dlg.show();
        }
    });
    dlg.exec();
}

/** Section title of a Windows 7 property sheet: dark text and a plain grey-blue line to the right */
class SheetHeader : public QWidget
{
public:
    explicit SheetHeader(const QString &text)
        : m_text(text)
    {
        setFixedHeight(QFontMetrics(font()).height() + 4);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setPen(QColor(0x1e, 0x1e, 0x1e));
        p.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, m_text);
        const int x = QFontMetrics(font()).horizontalAdvance(m_text) + 7;
        p.setPen(QColor(0xc5, 0xcf, 0xd8));
        p.drawLine(x, height() / 2 + 1, width() - 1, height() / 2 + 1);
    }

private:
    QString m_text;
};

/** Windows 7 "Local Area Connection Status" window */
class StatusDialog : public QDialog
{
public:
    StatusDialog(QWidget *parent, const Tile &tile, std::function<void()> changed)
        : QDialog(parent)
        , m_tile(tile)
        , m_changed(std::move(changed))
    {
        setWindowTitle(tr7("Status: %1").arg(tile.name));
        setAttribute(Qt::WA_DeleteOnClose);
        setFixedWidth(372);
        m_ipIface = tile.isAdapter() ? tile.iface : conValue(tile.uuid, QStringLiteral("GENERAL.IP-IFACE"));
        auto *v = new QVBoxLayout(this);
        auto *tabs = new QTabWidget;
        auto *page = new QWidget;
        auto *pv = new QVBoxLayout(page);

        // Windows 7 layout: headed sections (text + line), indented rows, no group boxes
        pv->setContentsMargins(10, 10, 10, 10);
        pv->setSpacing(0);
        pv->addWidget(new SheetHeader(tr7("Connections")));
        pv->addSpacing(4);
        auto *grid = new QGridLayout;
        grid->setContentsMargins(12, 0, 4, 0);
        grid->setVerticalSpacing(3);
        grid->setColumnStretch(1, 1);
        int r = 0;
        auto addRow = [&](const QString &label, QLabel *&valueLabel) {
            grid->addWidget(new QLabel(label), r, 0);
            valueLabel = rightAligned(QString());
            grid->addWidget(valueLabel, r, 1);
            ++r;
        };
        addRow(tr7("IPv4 Connectivity:"), m_v4);
        addRow(tr7("IPv6 Connectivity:"), m_v6);
        addRow(tr7("Media State:"), m_media);
        if (tile.kind == Tile::Wifi) {
            addRow(QStringLiteral("SSID:"), m_ssid);
        }
        addRow(tr7("Duration:"), m_duration);
        addRow(tr7("Speed:"), m_speed);
        if (tile.kind == Tile::Wifi) {
            addRow(tr7("Signal Quality:"), m_signal);
        }
        pv->addLayout(grid);
        pv->addSpacing(8);
        auto *detailsRow = new QHBoxLayout;
        detailsRow->setContentsMargins(12, 0, 0, 0);
        auto *details = win7Button(tr7("Details..."));
        detailsRow->addWidget(details);
        detailsRow->addStretch(1);
        pv->addLayout(detailsRow);
        pv->addSpacing(30);

        pv->addWidget(new SheetHeader(tr7("Activity")));
        pv->addSpacing(6);
        auto *ag = new QGridLayout;
        ag->setContentsMargins(12, 0, 4, 0);
        ag->setHorizontalSpacing(6);
        ag->setVerticalSpacing(2);
        auto *sent = new QLabel(tr7("Sent —"));
        auto *recv = new QLabel(tr7("— Received"));
        auto *ic = new QLabel;
        ic->setPixmap(QPixmap(QDir::homePath() + QStringLiteral("/.local/share/icons/hicolor/48x48/apps/seven-net-activity.png"))); // cut from Windows 7
        ag->addWidget(sent, 0, 1, Qt::AlignRight | Qt::AlignVCenter);
        ag->addWidget(ic, 0, 2, Qt::AlignCenter);
        ag->addWidget(recv, 0, 3, Qt::AlignLeft | Qt::AlignVCenter);
        ag->addWidget(new QLabel(tr7("Bytes:")), 1, 0);
        m_tx = rightAligned(QString());
        m_rx = rightAligned(QString());
        auto *sep = new QWidget;
        sep->setAttribute(Qt::WA_StyledBackground, true);
        sep->setStyleSheet(QStringLiteral("background: #808080;"));
        sep->setFixedSize(1, 16);
        ag->addWidget(m_tx, 1, 1);
        ag->addWidget(sep, 1, 2, Qt::AlignCenter);
        ag->addWidget(m_rx, 1, 3);
        ag->setColumnStretch(1, 1);
        ag->setColumnStretch(3, 1);
        pv->addLayout(ag);
        pv->addSpacing(22);

        auto *actions = new QHBoxLayout;
        actions->setSpacing(6);
        auto *props = win7Button(tr7("Properties"));
        props->setIcon(QIcon::fromTheme(QStringLiteral("seven-cp-shield")));
        auto *off = win7Button(tile.isAdapter() ? tr7("Disable") : tr7("Disconnect"));
        off->setIcon(QIcon::fromTheme(QStringLiteral("seven-cp-shield")));
        auto *diag = win7Button(tr7("Diagnose"));
        actions->addWidget(props);
        actions->addWidget(off);
        actions->addWidget(diag);
        actions->addStretch(1);
        pv->addLayout(actions);
        connect(diag, &QPushButton::clicked, this, [this] {
            diagnoseNetwork(this);
        });
        tabs->addTab(page, tr7("General"));
        v->addWidget(tabs);
        auto *closeRow = new QHBoxLayout;
        closeRow->addStretch(1);
        auto *close = win7Button(tr7("Close"));
        closeRow->addWidget(close);
        v->addLayout(closeRow);

        // Windows 7: no default button here, the keyboard starts on the tab
        for (QPushButton *b : findChildren<QPushButton *>()) {
            b->setAutoDefault(false);
        }
        tabs->setFocus();
        connect(close, &QPushButton::clicked, this, &QDialog::close);
        connect(details, &QPushButton::clicked, this, [this] {
            showDetails();
        });
        connect(props, &QPushButton::clicked, this, [this] {
            if (showProperties(this, m_tile)) {
                m_changed();
            }
        });
        connect(off, &QPushButton::clicked, this, [this] {
            if (m_tile.isAdapter()) {
                nmcli({QStringLiteral("dev"), QStringLiteral("disconnect"), m_tile.iface});
            } else {
                nmcli({QStringLiteral("con"), QStringLiteral("down"), m_tile.uuid});
            }
            m_changed();
            this->close();
        });
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] {
            refresh(false);
        });
        timer->start(1000);
        refresh(true);
    }

private:
    void refresh(bool full)
    {
        m_tx->setText(formatBytes(readSys(m_ipIface, QStringLiteral("statistics/tx_bytes")).toULongLong()));
        m_rx->setText(formatBytes(readSys(m_ipIface, QStringLiteral("statistics/rx_bytes")).toULongLong()));
        if (full && !m_tile.uuid.isEmpty()) {
            m_since = conValue(m_tile.uuid, QStringLiteral("connection.timestamp")).toLongLong(); // when it was activated
        }
        if (m_since > 0) {
            const qint64 d = qMax<qint64>(0, QDateTime::currentSecsSinceEpoch() - m_since);
            const qint64 days = d / 86400;
            const QString hms = QStringLiteral("%1:%2:%3").arg((d / 3600) % 24, 2, 10, QLatin1Char('0')).arg((d / 60) % 60, 2, 10, QLatin1Char('0')).arg(d % 60, 2, 10, QLatin1Char('0'));
            m_duration->setText(days > 0 ? tr7("%1 days %2").arg(days).arg(hms) : hms);
        } else {
            m_duration->setText(QStringLiteral("—"));
        }
        if (!full && ++m_tick % 5) {
            return;
        }
        const QString dev = m_tile.isAdapter() ? m_tile.iface : m_ipIface;
        m_v4->setText(connectivityText(devValue(dev, QStringLiteral("GENERAL.IP4-CONNECTIVITY"))));
        m_v6->setText(connectivityText(devValue(dev, QStringLiteral("GENERAL.IP6-CONNECTIVITY"))));
        m_media->setText(readSys(m_ipIface, QStringLiteral("operstate")) == QLatin1String("down") ? tr7("Off", "firewall state") : tr7("On", "state"));
        QString speed;
        if (m_tile.kind == Tile::Wifi) {
            const QList<QStringList> aps = terse({QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("IN-USE,SSID,SIGNAL,RATE"), QStringLiteral("dev"),
                                                  QStringLiteral("wifi"), QStringLiteral("list"), QStringLiteral("ifname"), m_tile.iface,
                                                  QStringLiteral("--rescan"), QStringLiteral("no")});
            for (const QStringList &ap : aps) {
                if (ap.value(0) == QLatin1String("*")) {
                    m_ssid->setText(ap.value(1));
                    m_signal->setText(QStringLiteral("%1 %").arg(ap.value(2)));
                    speed = QString(ap.value(3)).replace(QStringLiteral("Mbit/s"), tr7("Mbps"));
                }
            }
        } else {
            const int mbit = readSys(m_ipIface, QStringLiteral("speed")).toInt();
            if (mbit >= 1000) {
                speed = tr7("%1 Gbps").arg(QLocale{}.toString(mbit / 1000.0, 'f', 1));
            } else if (mbit > 0) {
                speed = tr7("%1 Mbps").arg(mbit);
            }
        }
        m_speed->setText(speed.isEmpty() ? QStringLiteral("—") : speed);
    }

    void showDetails()
    {
        // Windows 7 "Network Connection Details": every property in a fixed order, empty ones too
        QDialog dlg(this);
        dlg.setWindowTitle(tr7("Network Connection Details"));
        dlg.setFixedSize(404, 436);
        auto *v = new QVBoxLayout(&dlg);
        v->setContentsMargins(11, 8, 11, 11);
        v->setSpacing(4);
        v->addWidget(new QLabel(tr7("Network Connection Details:")));
        auto *tree = new QTreeWidget;
        tree->setRootIsDecorated(false);
        tree->setUniformRowHeights(true);
        tree->setHeaderLabels({tr7("Property"), tr7("Value")});
        tree->setColumnWidth(0, 160);
        tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        tree->header()->setStretchLastSection(false);
        tree->setItemDelegate(new CompactRowDelegate(tree)); // Windows 7 list rows are 17 px
        const QString dev = m_tile.isAdapter() ? m_tile.iface : m_ipIface;
        const QList<QStringList> rows = terse({QStringLiteral("-t"), QStringLiteral("dev"), QStringLiteral("show"), dev});
        QMap<QString, QStringList> info;
        for (const QStringList &r : rows) {
            info[r.value(0).section(QLatin1Char('['), 0, 0)] << r.mid(1).join(QLatin1Char(':'));
        }
        auto add = [tree](const QString &k, const QString &val) {
            new QTreeWidgetItem(tree, {k, val});
        };
        const bool dhcp = !m_tile.uuid.isEmpty() && conValue(m_tile.uuid, QStringLiteral("ipv4.method")) == QLatin1String("auto");
        const QLocale uk;
        const QString dateFmt = tr7("dddd, MMMM d, yyyy h:mm:ss AP");
        QString expiry, obtained, server, suffix;
        qint64 expirySecs = 0, leaseSecs = 0;
        // the DHCP lease is shown by the connection, not by the device
        QStringList dhcpOpts = info.value(QStringLiteral("DHCP4.OPTION"));
        if (dhcpOpts.isEmpty() && !m_tile.uuid.isEmpty()) {
            for (const QStringList &r : terse({QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("DHCP4"), QStringLiteral("con"), QStringLiteral("show"), m_tile.uuid})) {
                dhcpOpts << r.mid(1).join(QLatin1Char(':'));
            }
        }
        for (const QString &opt : std::as_const(dhcpOpts)) {
            const QString k = opt.section(QLatin1Char('='), 0, 0).trimmed();
            const QString val = opt.section(QLatin1Char('='), 1).trimmed();
            if (k == QLatin1String("expiry")) {
                expirySecs = val.toLongLong();
            } else if (k == QLatin1String("dhcp_lease_time")) {
                leaseSecs = val.toLongLong();
            } else if (k == QLatin1String("dhcp_server_identifier")) {
                server = val;
            } else if (k == QLatin1String("domain_name")) {
                suffix = val;
            }
        }
        if (expirySecs > 0) {
            expiry = uk.toString(QDateTime::fromSecsSinceEpoch(expirySecs), dateFmt);
            if (leaseSecs > 0) {
                obtained = uk.toString(QDateTime::fromSecsSinceEpoch(expirySecs - leaseSecs), dateFmt);
            }
        }
        const QStringList v4 = info.value(QStringLiteral("IP4.ADDRESS"));
        QString mac = info.value(QStringLiteral("GENERAL.HWADDR")).value(0);
        add(tr7("Connection-specific DNS Suffix"), suffix);
        add(tr7("Description"), m_tile.description);
        add(tr7("Physical Address"), mac.replace(QLatin1Char(':'), QLatin1Char('-')));
        add(tr7("DHCP Enabled"), dhcp ? tr7("Yes") : tr7("No"));
        add(tr7("IPv4 Address"), v4.value(0).section(QLatin1Char('/'), 0, 0));
        add(tr7("IPv4 Subnet Mask"), v4.isEmpty() ? QString() : prefixToMask(v4.value(0).section(QLatin1Char('/'), 1, 1).toInt()));
        for (const QString &a : v4.mid(1)) {
            add(tr7("IPv4 Address"), a.section(QLatin1Char('/'), 0, 0));
        }
        if (dhcp) {
            add(tr7("Lease Obtained"), obtained);
            add(tr7("Lease Expires"), expiry);
        }
        add(tr7("IPv4 Default Gateway"), info.value(QStringLiteral("IP4.GATEWAY")).value(0).replace(QStringLiteral("--"), QString()));
        if (dhcp) {
            add(tr7("IPv4 DHCP Server"), server);
        }
        const QStringList dns = info.value(QStringLiteral("IP4.DNS"));
        add(tr7("IPv4 DNS Server"), dns.value(0));
        for (const QString &d : dns.mid(1)) {
            add(QString(), d);
        }
        add(tr7("IPv4 WINS Server"), QString());
        add(tr7("NetBIOS over Tcpip Enabled"), tr7("Yes"));
        QStringList global6;
        QString link6;
        for (const QString &a : info.value(QStringLiteral("IP6.ADDRESS"))) {
            (a.startsWith(QLatin1String("fe80")) ? link6 : global6.emplaceBack()) = a.section(QLatin1Char('/'), 0, 0);
        }
        for (const QString &a : std::as_const(global6)) {
            add(tr7("IPv6 Address"), a);
        }
        add(tr7("Link-local IPv6 Address"), link6.isEmpty() ? QString() : link6 + QStringLiteral("%") + readSys(m_ipIface, QStringLiteral("ifindex")));
        const QString gw6 = info.value(QStringLiteral("IP6.GATEWAY")).value(0);
        add(tr7("IPv6 Default Gateway"), gw6 == QLatin1String("--") ? QString() : gw6);
        const QStringList dns6 = info.value(QStringLiteral("IP6.DNS"));
        add(tr7("IPv6 DNS Server"), dns6.value(0));
        for (const QString &d : dns6.mid(1)) {
            add(QString(), d);
        }
        tree->resizeColumnToContents(1);
        v->addWidget(tree, 1);
        v->addSpacing(6);
        auto *row = new QHBoxLayout;
        row->addStretch(1);
        auto *close = win7Button(tr7("Close"));
        close->setDefault(true);
        row->addWidget(close);
        v->addLayout(row);
        connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
        dlg.exec();
    }

    Tile m_tile;
    std::function<void()> m_changed;
    QString m_ipIface;
    QLabel *m_v4 = nullptr, *m_v6 = nullptr, *m_media = nullptr, *m_ssid = nullptr, *m_speed = nullptr, *m_signal = nullptr;
    QLabel *m_tx = nullptr, *m_rx = nullptr, *m_duration = nullptr;
    qint64 m_since = 0;
    int m_tick = 0;
};

inline void showStatus(QWidget *parent, const Tile &t, const std::function<void()> &changed)
{
    auto *dlg = new StatusDialog(parent, t, changed);
    dlg->show();
}

/* ---------------- "Set Up a Connection or Network" wizard ---------------- */

/** Command link like on the wizard pages: icon, bold-ish title, grey description */
class CommandLink : public QWidget
{
public:
    CommandLink(const QString &icon, const QString &title, const QString &desc, std::function<void()> action)
        : m_action(std::move(action))
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(8, 6, 8, 8);
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(icon).pixmap(32, 32));
        h->addWidget(ic, 0, Qt::AlignTop);
        auto *v = new QVBoxLayout;
        v->setSpacing(2);
        auto *t = new QLabel(title);
        QFont f = t->font();
        f.setPixelSize(14);
        t->setFont(f);
        t->setStyleSheet(QStringLiteral("color: #151c55;"));
        v->addWidget(t);
        auto *d = plain(desc);
        d->setStyleSheet(QStringLiteral("color: #151c55;"));
        v->addWidget(d);
        h->addLayout(v, 1);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!underMouse()) {
            return;
        }
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QColor(0xc6, 0xc6, 0xc6));
        p.setBrush(QColor(0xf8, 0xf8, 0xf8));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    }
    void enterEvent(QEnterEvent *) override
    {
        update();
    }
    void leaveEvent(QEvent *) override
    {
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) && m_action) {
            m_action();
        }
    }

private:
    std::function<void()> m_action;
};

class WizardPage : public QWizardPage
{
public:
    std::function<int()> next;
    std::function<bool()> validate;
    std::function<void()> init;
    bool finalPage = false;
    int nextId() const override
    {
        return next ? next() : -1;
    }
    bool validatePage() override
    {
        return validate ? validate() : true;
    }
    void initializePage() override
    {
        if (init) {
            init();
        }
    }
    bool isComplete() const override
    {
        return complete;
    }
    void setComplete(bool c)
    {
        complete = c;
        Q_EMIT completeChanged();
    }
    bool complete = true;
};

/** First Ethernet adapter, used as the parent of new PPPoE connections */
inline QString wiredIface()
{
    for (const QStringList &d : terse({QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("DEVICE,TYPE"), QStringLiteral("dev")})) {
        if (d.value(1) == QLatin1String("ethernet")) {
            return d.value(0);
        }
    }
    return {};
}

inline QLineEdit *hintEdit(const QString &hint, bool password = false)
{
    auto *e = new QLineEdit;
    e->setPlaceholderText(hint);
    if (password) {
        e->setEchoMode(QLineEdit::Password);
    }
    return e;
}

/** Options of the wizard's first page: icon, title and grey description, blue highlight behind the text only */
class OptionDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override
    {
        p->save();
        const QRect r = opt.rect;
        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        icon.paint(p, QRect(r.left() + 8, r.top() + (r.height() - 32) / 2, 32, 32));
        const QStringList lines = index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n'));
        const QFontMetrics fm(opt.font);
        const int x = r.left() + 46;
        int w = 0;
        for (const QString &l : lines) {
            w = qMax(w, fm.horizontalAdvance(l));
        }
        const int h = fm.height() * lines.size();
        const int y = r.top() + (r.height() - h) / 2;
        const bool sel = opt.state & QStyle::State_Selected;
        if (sel) {
            p->fillRect(QRect(x - 2, y - 1, w + 4, h + 2), QColor(0x33, 0x99, 0xff));
        }
        p->setFont(opt.font);
        for (int i = 0; i < lines.size(); ++i) {
            p->setPen(sel ? QColor(Qt::white) : i == 0 ? QColor(Qt::black) : QColor(0x6d, 0x6d, 0x6d));
            p->drawText(QRect(x, y + i * fm.height(), w + 2, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, lines.at(i));
        }
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &) const override
    {
        return QSize(200, QFontMetrics(opt.font).height() * 2 + 22);
    }
};

inline void newConnectionWizard(QWidget *parent, const std::function<void()> &changed)
{
    enum { Choose, Internet, Pppoe, VpnHow, VpnAddress, VpnCredentials, Result };
    auto *wiz = new Aero::Wizard(parent);
    wiz->setAttribute(Qt::WA_DeleteOnClose);
    wiz->setWindowTitle(tr7("Set Up a Connection or Network"));
    wiz->setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-net-center")));
    wiz->setButtonText(QWizard::NextButton, tr7("Next"));
    wiz->setButtonText(QWizard::CancelButton, tr7("Cancel"));
    wiz->setButtonText(QWizard::FinishButton, tr7("Close"));
    wiz->setButtonText(QWizard::BackButton, tr7("Back"));
    wiz->setOption(QWizard::NoBackButtonOnStartPage, true);
    wiz->resize(620, 460);

    struct State {
        QString uuid;
        QString name;
        QByteArray secrets;
        bool connectNow = true;
        bool vpn = false;
    };
    auto state = std::make_shared<State>();

    auto page = [](const QString &title) {
        auto *p = new WizardPage;
        auto *v = new QVBoxLayout(p);
        v->setContentsMargins(20, 10, 20, 10);
        auto *t = blueTitle(title, 16);
        t->setObjectName(QStringLiteral("win7wiztitle"));
        v->addWidget(t);
        v->addSpacing(16);
        return p;
    };

    // 1. Choose a connection option
    auto *choose = page(tr7("Choose a connection option"));
    auto *list = new QListWidget;
    list->setIconSize(QSize(32, 32));
    list->setItemDelegate(new OptionDelegate(list));
    list->setFocusPolicy(Qt::NoFocus);
    list->setStyleSheet(QStringLiteral("QListWidget { border: 1px solid #828790; background: #ffffff; padding: 4px; }"));
    auto addOption = [list](const QString &icon, const QString &title, const QString &desc, int target) {
        auto *item = new QListWidgetItem(QIcon::fromTheme(icon), title + QLatin1Char('\n') + desc, list);
        item->setData(Qt::UserRole, target);
    };
    addOption(QStringLiteral("seven-net-wiz-internet"), tr7("Connect to the Internet"),
              tr7("Set up a broadband (PPPoE) connection to the Internet."), Internet);
    if (hasPptp()) {
        addOption(QStringLiteral("network-vpn"), tr7("Connect to a workplace"), tr7("Set up a VPN connection to your workplace."), VpnHow);
    }
    list->setCurrentRow(0);
    choose->layout()->addWidget(list);
    choose->next = [list] {
        return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toInt() : -1;
    };
    QObject::connect(list, &QListWidget::itemDoubleClicked, wiz, [wiz] {
        wiz->next();
    });
    wiz->setPage(Choose, choose);

    // 2. How do you want to connect? (Internet)
    auto *internet = page(tr7("How do you want to connect?"));
    internet->layout()->addWidget(new CommandLink(QStringLiteral("network-modem"), tr7("Broadband (PPPoE)"),
                                                  tr7("Connect using DSL or cable that requires a user name and password."),
                                                  [wiz] {
                                                      wiz->next();
                                                  }));
    static_cast<QVBoxLayout *>(internet->layout())->addStretch(1);
    internet->next = [] {
        return int(Pppoe);
    };
    internet->init = [wiz] {
        wiz->button(QWizard::NextButton)->hide();
    };
    wiz->setPage(Internet, internet);

    // 3. PPPoE details
    auto *pppoe = page(tr7("Type the information from your Internet service provider (ISP)"));
    auto *pf = new QFormLayout;
    pf->setHorizontalSpacing(16);
    pf->setVerticalSpacing(8);
    auto *pUser = hintEdit(tr7("[Name your ISP gave you]"));
    auto *pPass = hintEdit(tr7("[Password your ISP gave you]"), true);
    auto *pShow = new QCheckBox(tr7("Show characters"));
    auto *pRemember = new QCheckBox(tr7("Remember this password"));
    auto *pName = new QLineEdit(tr7("Broadband Connection"));
    auto *pOthers = new QCheckBox(tr7("Allow other people to use this connection"));
    pf->addRow(tr7("User name:"), pUser);
    pf->addRow(tr7("Password:"), pPass);
    pf->addRow(QString(), pShow);
    pf->addRow(QString(), pRemember);
    pf->addRow(tr7("Connection name:"), pName);
    static_cast<QVBoxLayout *>(pppoe->layout())->addLayout(pf);
    pppoe->layout()->addWidget(new QLabel);
    auto *othersRow = new QHBoxLayout;
    auto *shield = new QLabel;
    shield->setPixmap(QIcon::fromTheme(QStringLiteral("seven-cp-shield")).pixmap(16, 16));
    othersRow->addWidget(shield);
    othersRow->addWidget(pOthers, 1);
    static_cast<QVBoxLayout *>(pppoe->layout())->addLayout(othersRow);
    auto *othersNote = plain(tr7("This option allows anyone with access to this computer to use this connection."));
    othersNote->setContentsMargins(22, 0, 0, 0);
    pppoe->layout()->addWidget(othersNote);
    static_cast<QVBoxLayout *>(pppoe->layout())->addStretch(1);
    QObject::connect(pShow, &QCheckBox::toggled, pPass, [pPass](bool on) {
        pPass->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
    });
    pppoe->init = [wiz] {
        wiz->button(QWizard::NextButton)->show();
        wiz->setButtonText(QWizard::NextButton, tr7("Connect"));
    };
    pppoe->next = [] {
        return int(Result);
    };
    pppoe->validate = [=] {
        const QString iface = wiredIface();
        if (iface.isEmpty()) {
            QMessageBox::warning(wiz, wiz->windowTitle(), tr7("No wired network adapter was found for a broadband connection."));
            return false;
        }
        if (pUser->text().isEmpty()) {
            QMessageBox::warning(wiz, wiz->windowTitle(), tr7("Enter a user name."));
            return false;
        }
        QStringList args{QStringLiteral("con"), QStringLiteral("add"), QStringLiteral("type"), QStringLiteral("pppoe"), QStringLiteral("con-name"), pName->text(),
                         QStringLiteral("ifname"), iface, QStringLiteral("pppoe.username"), pUser->text(), QStringLiteral("connection.autoconnect"), QStringLiteral("no")};
        if (pRemember->isChecked()) {
            args << QStringLiteral("pppoe.password") << pPass->text();
        } else {
            args << QStringLiteral("pppoe.password-flags") << QStringLiteral("2");
            state->secrets = QStringLiteral("pppoe.password:%1\n").arg(pPass->text()).toUtf8();
        }
        if (!pOthers->isChecked()) {
            args << QStringLiteral("connection.permissions") << QStringLiteral("user:%1").arg(userName());
        }
        const Win7Net::Result r = nmcli(args);
        if (!r.ok) {
            QMessageBox::warning(wiz, wiz->windowTitle(), tr7("The connection could not be created.\n\n%1").arg(r.err));
            return false;
        }
        static const QRegularExpression uuidRe(QStringLiteral("\\(([0-9a-f-]{36})\\)"));
        state->uuid = uuidRe.match(r.out).captured(1);
        state->name = pName->text();
        state->connectNow = true;
        state->vpn = false;
        changed();
        return true;
    };
    wiz->setPage(Pppoe, pppoe);

    // VPN: how do you want to connect?
    auto *vpnHow = page(tr7("How do you want to connect?"));
    vpnHow->layout()->addWidget(new CommandLink(QStringLiteral("seven-net-internet"), tr7("Use my Internet connection (VPN)"),
                                                tr7("Connect using a virtual private network (VPN) connection through the Internet."),
                                                [wiz] {
                                                    wiz->next();
                                                }));
    static_cast<QVBoxLayout *>(vpnHow->layout())->addStretch(1);
    vpnHow->next = [] {
        return int(VpnAddress);
    };
    vpnHow->init = [wiz] {
        wiz->button(QWizard::NextButton)->hide();
    };
    wiz->setPage(VpnHow, vpnHow);

    auto *vpnAddr = page(tr7("Type the Internet address to connect to"));
    vpnAddr->layout()->addWidget(plain(tr7("Your network administrator can give you this address.")));
    auto *af = new QFormLayout;
    auto *vHost = hintEdit(tr7("[Example: Contoso.com, 157.54.0.1 or 3ffe:1234::1111]"));
    auto *vName = new QLineEdit(tr7("VPN Connection"));
    af->addRow(tr7("Internet address:"), vHost);
    af->addRow(tr7("Destination name:"), vName);
    static_cast<QVBoxLayout *>(vpnAddr->layout())->addLayout(af);
    auto *vOthers = new QCheckBox(tr7("Allow other people to use this connection"));
    auto *vLater = new QCheckBox(tr7("Don't connect now; just set it up so I can connect later"));
    vpnAddr->layout()->addWidget(new QLabel);
    vpnAddr->layout()->addWidget(vOthers);
    vpnAddr->layout()->addWidget(vLater);
    static_cast<QVBoxLayout *>(vpnAddr->layout())->addStretch(1);
    vpnAddr->init = [wiz] {
        wiz->button(QWizard::NextButton)->show();
        wiz->setButtonText(QWizard::NextButton, tr7("Next"));
    };
    vpnAddr->validate = [=] {
        if (vHost->text().trimmed().isEmpty()) {
            QMessageBox::warning(wiz, wiz->windowTitle(), tr7("Enter an Internet address."));
            return false;
        }
        return true;
    };
    vpnAddr->next = [] {
        return int(VpnCredentials);
    };
    wiz->setPage(VpnAddress, vpnAddr);

    auto *vpnCred = page(tr7("Type your user name and password"));
    auto *cf = new QFormLayout;
    auto *vUser = new QLineEdit;
    auto *vPass = new QLineEdit;
    vPass->setEchoMode(QLineEdit::Password);
    auto *vShow = new QCheckBox(tr7("Show characters", "vpn"));
    auto *vRemember = new QCheckBox(tr7("Remember this password"));
    auto *vDomain = new QLineEdit;
    cf->addRow(tr7("User name:"), vUser);
    cf->addRow(tr7("Password:"), vPass);
    cf->addRow(QString(), vShow);
    cf->addRow(QString(), vRemember);
    cf->addRow(tr7("Domain (optional):"), vDomain);
    static_cast<QVBoxLayout *>(vpnCred->layout())->addLayout(cf);
    static_cast<QVBoxLayout *>(vpnCred->layout())->addStretch(1);
    QObject::connect(vShow, &QCheckBox::toggled, vPass, [vPass](bool on) {
        vPass->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
    });
    vpnCred->init = [=] {
        wiz->setButtonText(QWizard::NextButton, vLater->isChecked() ? tr7("Create") : tr7("Connect"));
    };
    vpnCred->next = [] {
        return int(Result);
    };
    vpnCred->validate = [=] {
        QString data = QStringLiteral("gateway = %1, user = %2").arg(vHost->text().trimmed(), vUser->text());
        if (!vDomain->text().isEmpty()) {
            data += QStringLiteral(", domain = %1").arg(vDomain->text());
        }
        data += QStringLiteral(", password-flags = %1").arg(vRemember->isChecked() ? 0 : 2);
        QStringList args{QStringLiteral("con"), QStringLiteral("add"), QStringLiteral("type"), QStringLiteral("vpn"), QStringLiteral("vpn-type"), QStringLiteral("pptp"),
                         QStringLiteral("con-name"), vName->text(), QStringLiteral("vpn.data"), data, QStringLiteral("connection.autoconnect"), QStringLiteral("no")};
        if (vRemember->isChecked() && !vPass->text().isEmpty()) {
            args << QStringLiteral("vpn.secrets") << QStringLiteral("password = %1").arg(vPass->text());
        } else {
            state->secrets = QStringLiteral("vpn.secrets.password:%1\n").arg(vPass->text()).toUtf8();
        }
        if (!vOthers->isChecked()) {
            args << QStringLiteral("connection.permissions") << QStringLiteral("user:%1").arg(userName());
        }
        const Win7Net::Result r = nmcli(args);
        if (!r.ok) {
            QMessageBox::warning(wiz, wiz->windowTitle(), tr7("The connection could not be created.\n\n%1").arg(r.err));
            return false;
        }
        static const QRegularExpression uuidRe(QStringLiteral("\\(([0-9a-f-]{36})\\)"));
        state->uuid = uuidRe.match(r.out).captured(1);
        state->name = vName->text();
        state->connectNow = !vLater->isChecked();
        state->vpn = true;
        changed();
        return true;
    };
    wiz->setPage(VpnCredentials, vpnCred);

    // Result: connect and report
    auto *result = page(QString());
    auto *resultIcon = new QLabel;
    auto *resultText = plain(QString());
    auto *resultRow = new QHBoxLayout;
    resultRow->addWidget(resultIcon, 0, Qt::AlignTop);
    resultRow->addWidget(resultText, 1);
    static_cast<QVBoxLayout *>(result->layout())->addLayout(resultRow);
    static_cast<QVBoxLayout *>(result->layout())->addStretch(1);
    result->finalPage = true;
    result->setFinalPage(true);
    result->init = [=] {
        auto *title = result->findChild<QLabel *>(QStringLiteral("win7wiztitle"));
        wiz->button(QWizard::BackButton)->setEnabled(false);
        if (!state->connectNow) {
            title->setText(tr7("The connection is ready to use"));
            resultText->setText(tr7("The connection \"%1\" was created. You can find it in Network Connections.").arg(state->name));
            return;
        }
        title->setText(state->vpn ? tr7("Connecting to %1...").arg(state->name) : tr7("Connecting to the Internet..."));
        resultIcon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-net-computers")).pixmap(32, 32));
        resultText->setText(tr7("Please wait while connecting..."));
        result->setComplete(false);
        QTimer::singleShot(100, result, [=] {
            QProcess proc;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
            proc.setProcessEnvironment(env);
            QStringList args{QStringLiteral("con"), QStringLiteral("up"), state->uuid};
            if (!state->secrets.isEmpty()) {
                args << QStringLiteral("passwd-file") << QStringLiteral("/dev/stdin");
            }
            QEventLoop loop;
            QObject::connect(&proc, &QProcess::finished, &loop, &QEventLoop::quit);
            proc.start(QStringLiteral("nmcli"), args);
            proc.write(state->secrets);
            proc.closeWriteChannel();
            loop.exec();
            const bool ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
            changed();
            if (ok) {
                title->setText(state->vpn ? tr7("The connection to %1 is ready to use").arg(state->name)
                                          : tr7("The connection to the Internet is ready to use"));
                resultIcon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-net-internet")).pixmap(32, 32));
                resultText->setText(QString());
            } else {
                QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
                err.remove(QRegularExpression(QStringLiteral("^Error: ")));
                title->setText(tr7("The connection failed"));
                resultIcon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-net-warn")).pixmap(32, 32));
                resultText->setText(tr7("%1\n\nThe connection \"%2\" was saved. You can try again in Network Connections.").arg(err, state->name));
            }
            result->setComplete(true);
        });
    };
    wiz->setPage(Result, result);
    wiz->show();
}

/* ---------------- "Network Connections" ---------------- */

class TileWidget : public QWidget
{
public:
    explicit TileWidget(const Tile &t, QWidget *parent)
        : QWidget(parent)
        , tile(t)
    {
        setAttribute(Qt::WA_Hover);
        setFixedSize(300, 60);
        m_icon = tileIcon(t);
    }
    Tile tile;
    bool selected = false;
    std::function<void(TileWidget *)> onClick, onDoubleClick;
    std::function<void(TileWidget *, QPoint)> onMenu;

    QRect nameRect() const
    {
        return QRect(56, 5, width() - 60, 16);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool hot = underMouse();
        if (selected || hot) {
            QLinearGradient g(0, 0, 0, height());
            if (selected) {
                g.setColorAt(0, hot ? QColor(0xe8, 0xf2, 0xfe) : QColor(0xdc, 0xeb, 0xfc));
                g.setColorAt(1, hot ? QColor(0xc4, 0xde, 0xfc) : QColor(0xc1, 0xdb, 0xfc));
                p.setPen(hot ? QColor(0x7d, 0xa2, 0xce) : QColor(0x84, 0xac, 0xdd));
            } else {
                g.setColorAt(0, QColor(0xfa, 0xfb, 0xfd));
                g.setColorAt(1, QColor(0xeb, 0xf3, 0xfd));
                p.setPen(QColor(0xb8, 0xd6, 0xfb));
            }
            p.setBrush(g);
            p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        }
        p.drawPixmap(4, 6, m_icon);
        const QFontMetrics fm(font());
        const int w = width() - 60;
        p.setPen(Qt::black);
        p.drawText(QRect(56, 5, w, 16), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(tile.name, Qt::ElideRight, w));
        p.setPen(QColor(0x6d, 0x6d, 0x6d));
        p.drawText(QRect(56, 22, w, 16), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(tile.status, Qt::ElideRight, w));
        p.drawText(QRect(56, 39, w, 16), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(tile.description, Qt::ElideRight, w));
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

/** White area with the tiles in rows, like the Explorer "Tiles" view */
class ConnectionsPage : public QWidget
{
public:
    ConnectionsPage(QToolBar *bar, std::function<void()> closeWindow)
        : m_bar(bar)
        , m_close(std::move(closeWindow))
    {
        setObjectName(QStringLiteral("win7page"));
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
        setFocusPolicy(Qt::StrongFocus);
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, [this] {
            const QString sig = signature();
            if (sig != m_signature) {
                reload();
            }
        });
        m_timer->start(2000);
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
        const QString keep = m_selected ? m_selected->tile.key() : QString();
        qDeleteAll(m_tiles);
        m_tiles.clear();
        m_selected = nullptr;
        for (const Tile &t : loadTiles()) {
            auto *w = new TileWidget(t, this);
            w->onClick = [this](TileWidget *tw) {
                select(tw);
            };
            w->onDoubleClick = [this](TileWidget *tw) {
                open(tw->tile);
            };
            w->onMenu = [this](TileWidget *tw, QPoint pos) {
                contextMenu(tw, pos);
            };
            m_tiles << w;
            if (t.key() == keep) {
                m_selected = w;
                w->selected = true;
            }
            w->show();
        }
        relayout();
        updateBar();
    }

protected:
    void resizeEvent(QResizeEvent *) override
    {
        relayout();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        select(nullptr);
        setFocus();
        if (e->button() == Qt::RightButton) {
            QMenu menu(this);
            menu.addAction(tr7("Refresh"), this, [this] {
                reload();
            });
            menu.exec(e->globalPosition().toPoint());
        }
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        if (!m_selected) {
            return QWidget::keyPressEvent(e);
        }
        if (e->key() == Qt::Key_F2) {
            rename(m_selected);
        } else if (e->key() == Qt::Key_Delete) {
            remove(m_selected->tile);
        } else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            open(m_selected->tile);
        } else {
            QWidget::keyPressEvent(e);
        }
    }

private:
    static QString signature()
    {
        return nmcli({QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("DEVICE,STATE,CON-UUID"), QStringLiteral("dev")}).out
            + nmcli({QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("UUID,NAME,ACTIVE"), QStringLiteral("con"), QStringLiteral("show")}).out;
    }

    void relayout()
    {
        const int cols = qMax(1, (width() - 8) / 304);
        int k = 0;
        for (TileWidget *w : std::as_const(m_tiles)) {
            const bool match = m_filter.isEmpty() || w->tile.name.contains(m_filter, Qt::CaseInsensitive)
                || w->tile.description.contains(m_filter, Qt::CaseInsensitive);
            w->setVisible(match);
            if (!match) {
                continue;
            }
            w->move(6 + (k % cols) * 304, 6 + (k / cols) * 64);
            ++k;
        }
        setMinimumHeight(12 + ((k + cols - 1) / cols) * 64);
    }

    void select(TileWidget *w)
    {
        for (TileWidget *t : std::as_const(m_tiles)) {
            t->selected = (t == w);
            t->update();
        }
        m_selected = w;
        updateBar();
    }

    void changed()
    {
        QTimer::singleShot(300, this, [this] {
            reload();
        });
    }

    /** Double click: connect a dial-up/VPN connection, otherwise show the status */
    void open(const Tile &t)
    {
        if (t.isAdapter()) {
            if (t.grey) {
                toggle(t);
            } else if (t.active) {
                showStatus(this, t, [this] {
                    changed();
                });
            }
            return;
        }
        if (t.active) {
            showStatus(this, t, [this] {
                changed();
            });
        } else {
            connectDialog(this, t, [this] {
                changed();
            });
        }
    }

    /** Enable/disable an adapter, connect/disconnect a dial-up or VPN connection */
    void toggle(const Tile &t)
    {
        if (t.kind == Tile::Ethernet) {
            nmcli({QStringLiteral("dev"), t.grey ? QStringLiteral("connect") : QStringLiteral("disconnect"), t.iface});
        } else if (t.kind == Tile::Wifi) {
            nmcli({QStringLiteral("radio"), QStringLiteral("wifi"), t.grey ? QStringLiteral("on") : QStringLiteral("off")});
        } else if (t.active) {
            nmcli({QStringLiteral("con"), QStringLiteral("down"), t.uuid});
        } else {
            connectDialog(this, t, [this] {
                changed();
            });
        }
        changed();
    }

    void remove(const Tile &t)
    {
        if (t.isAdapter()) {
            return;
        }
        if (!askYesNo(this, tr7("Confirm Connection Delete"), tr7("Are you sure you want to delete the connection \"%1\"?").arg(t.name),
                      QMessageBox::Warning)) {
            return;
        }
        const Result r = nmcli({QStringLiteral("con"), QStringLiteral("delete"), t.uuid});
        if (!r.ok) {
            QMessageBox::warning(this, tr7("Network Connections"), r.err);
        }
        changed();
    }

    void rename(TileWidget *w)
    {
        if (w->tile.uuid.isEmpty() || w->tile.kind == Tile::Wifi) {
            return;
        }
        auto *edit = new QLineEdit(w->tile.name, w);
        edit->setGeometry(w->nameRect().adjusted(-3, -2, 0, 2));
        edit->selectAll();
        edit->show();
        edit->setFocus();
        m_timer->stop();
        auto done = std::make_shared<bool>(false);
        auto commit = [this, edit, w, done](bool apply) {
            if (*done) {
                return;
            }
            *done = true;
            const QString name = edit->text().trimmed();
            if (apply && !name.isEmpty() && name != w->tile.name) {
                modify(this, w->tile.uuid, {QStringLiteral("connection.id"), name});
            }
            edit->deleteLater();
            m_timer->start(2000);
            changed();
        };
        connect(edit, &QLineEdit::returnPressed, edit, [commit] {
            commit(true);
        });
        connect(edit, &QLineEdit::editingFinished, edit, [commit] {
            commit(true);
        });
        auto *esc = new QAction(edit);
        esc->setShortcut(Qt::Key_Escape);
        esc->setShortcutContext(Qt::WidgetShortcut);
        edit->addAction(esc);
        connect(esc, &QAction::triggered, edit, [commit] {
            commit(false);
        });
    }

    void createShortcut(const Tile &t)
    {
        if (!askYesNo(this, tr7("Shortcut"), tr7("Windows cannot create a shortcut here.\nDo you want the shortcut to be placed on the desktop instead?"), QMessageBox::Warning)) {
            return;
        }
        const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        const QString exe = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/.local/bin/seven-control");
        const QString arg = t.isAdapter() ? QStringLiteral("--net-status %1").arg(t.iface) : QStringLiteral("--net-connect %1").arg(t.uuid);
        const QString icon = t.kind == Tile::Wifi ? QStringLiteral("seven-net-bars5") : t.kind == Tile::Ethernet ? QStringLiteral("seven-net-plug") : QStringLiteral("seven-net-computers");
        QString file = desktop + tr7("/%1 - Shortcut.desktop").arg(QString(t.name).replace(QLatin1Char('/'), QLatin1Char('-')));
        QFile f(file);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return;
        }
        f.write(tr7("[Desktop Entry]\nType=Application\nName=%1 - Shortcut\nIcon=%2\nExec=%3 %4\nTerminal=false\n").arg(t.name, icon, exe, arg).toUtf8());
        f.close();
        f.setPermissions(f.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    }

    void cloneConnection(const Tile &t)
    {
        const Result r = nmcli({QStringLiteral("con"), QStringLiteral("clone"), t.uuid, tr7("Copy of %1").arg(t.name)});
        if (!r.ok) {
            QMessageBox::warning(this, tr7("Network Connections"), r.err);
        }
        changed();
    }

    void properties(const Tile &t)
    {
        if (showProperties(this, t)) {
            changed();
        }
    }

    QString toggleText(const Tile &t, bool menu) const
    {
        if (t.isAdapter()) {
            return menu ? (t.grey ? tr7("Turn on") : tr7("Disable"))
                        : (t.grey ? tr7("Enable this network device") : tr7("Disable this network device"));
        }
        return menu ? (t.active ? tr7("Disconnect") : tr7("Connect"))
                    : (t.active ? tr7("Disconnect", "network") : tr7("Start the connection"));
    }

    void contextMenu(TileWidget *w, QPoint pos)
    {
        const Tile t = w->tile;
        const QIcon shield = QIcon::fromTheme(QStringLiteral("seven-cp-shield"));
        QMenu menu(this);
        QAction *def = menu.addAction(toggleText(t, true), this, [this, t] {
            toggle(t);
        });
        menu.setDefaultAction(def);
        QAction *status = menu.addAction(tr7("Status"), this, [this, t] {
            showStatus(this, t, [this] {
                changed();
            });
        });
        status->setEnabled(t.active);
        menu.addSeparator();
        if (!t.isAdapter()) {
            menu.addAction(tr7("Create Copy"), this, [this, t] {
                cloneConnection(t);
            });
            menu.addSeparator();
        }
        menu.addAction(tr7("Create Shortcut"), this, [this, t] {
            createShortcut(t);
        });
        QAction *del = menu.addAction(shield, tr7("Delete"), this, [this, t] {
            remove(t);
        });
        del->setEnabled(!t.isAdapter());
        QAction *ren = menu.addAction(shield, tr7("Rename"), this, [this, w] {
            rename(w);
        });
        ren->setEnabled(!t.uuid.isEmpty() && t.kind != Tile::Wifi);
        menu.addSeparator();
        QAction *props = menu.addAction(shield, tr7("Properties"), this, [this, t] {
            properties(t);
        });
        props->setEnabled(!t.uuid.isEmpty());
        menu.exec(pos);
    }

    void updateBar()
    {
        if (!m_bar) {
            return;
        }
        m_bar->clear();
        auto *organize = new QToolButton(m_bar);
        organize->setText(tr7("Organize"));
        organize->setToolButtonStyle(Qt::ToolButtonTextOnly);
        organize->setPopupMode(QToolButton::InstantPopup);
        auto *om = new QMenu(organize);
        TileWidget *w = m_selected;
        QAction *del = om->addAction(tr7("Delete"), this, [this, w] {
            if (w) {
                remove(w->tile);
            }
        });
        QAction *ren = om->addAction(tr7("Rename"), this, [this, w] {
            if (w) {
                rename(w);
            }
        });
        om->addSeparator();
        QAction *props = om->addAction(tr7("Properties"), this, [this, w] {
            if (w) {
                properties(w->tile);
            }
        });
        om->addSeparator();
        om->addAction(tr7("Close"), this, [this] {
            m_close();
        });
        del->setEnabled(w && !w->tile.isAdapter());
        ren->setEnabled(w && !w->tile.uuid.isEmpty() && w->tile.kind != Tile::Wifi);
        props->setEnabled(w && !w->tile.uuid.isEmpty());
        organize->setMenu(om);
        m_bar->addWidget(organize);
        if (!w) {
            return;
        }
        const Tile t = w->tile;
        m_bar->addAction(toggleText(t, false), this, [this, t] {
            toggle(t);
        });
        if (!t.uuid.isEmpty() && t.kind != Tile::Wifi) {
            m_bar->addAction(tr7("Rename this connection"), this, [this, w] {
                rename(w);
            });
        }
        if (t.active) {
            m_bar->addAction(tr7("View status of this connection"), this, [this, t] {
                showStatus(this, t, [this] {
                    changed();
                });
            });
        }
        if (!t.isAdapter()) {
            m_bar->addAction(tr7("Delete this connection"), this, [this, t] {
                remove(t);
            });
        }
        if (!t.uuid.isEmpty()) {
            m_bar->addAction(tr7("Change settings of this connection"), this, [this, t] {
                properties(t);
            });
        }
    }

    QToolBar *m_bar;
    std::function<void()> m_close;
    QList<TileWidget *> m_tiles;
    TileWidget *m_selected = nullptr;
    QString m_filter;
    QString m_signature;
    QTimer *m_timer;
};

/* ---------------- Network and Sharing Center ---------------- */

struct Nav {
    std::function<void(int)> open; // switch the Control Panel to a view
    std::function<void()> home;
};

inline QWidget *mapLine(bool broken)
{
    auto *w = new QWidget;
    w->setFixedSize(86, 32);
    struct Painter : public QObject {
        bool broken;
        bool eventFilter(QObject *o, QEvent *e) override
        {
            if (e->type() != QEvent::Paint) {
                return false;
            }
            auto *w = static_cast<QWidget *>(o);
            QPainter p(w);
            QLinearGradient g(0, 0, w->width(), 0);
            g.setColorAt(0, QColor(0xb4, 0xb4, 0xb4));
            g.setColorAt(0.5, QColor(0xd8, 0xd8, 0xd8));
            g.setColorAt(1, QColor(0xb4, 0xb4, 0xb4));
            p.fillRect(QRect(4, 15, w->width() - 8, 3), g);
            if (broken) {
                p.drawPixmap(w->width() / 2 - 8, 8, QIcon::fromTheme(QStringLiteral("seven-net-x")).pixmap(16, 16));
            }
            return true;
        }
    };
    auto *painter = new Painter;
    painter->broken = broken;
    painter->setParent(w);
    w->installEventFilter(painter);
    return w;
}

inline QWidget *mapNode(const QString &icon, const QString &text)
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    auto *ic = new QLabel;
    ic->setPixmap(QIcon::fromTheme(icon).pixmap(32, 32));
    ic->setAlignment(Qt::AlignCenter);
    v->addWidget(ic);
    auto *t = new QLabel(text);
    t->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    w->setFixedWidth(110);
    t->setWordWrap(true);
    v->addWidget(t);
    return w;
}

inline void openNetworkSettings()
{
    launch(QStringLiteral("/usr/bin/kcmshell6"), {QStringLiteral("kcm_networkmanagement")});
}

/** Windows 7 "Connect to a network" / "Connect or disconnect": the list of networks pops up from the tray icon.
 *  The tray applet (io.gitgud.wackyideas.networkmanagement) watches for this file and expands itself. */
inline void openNetworkFlyout()
{
    QFile f(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/seven-net-flyout"));
    f.open(QIODevice::WriteOnly);
}

/** One task: icon, blue link, grey text below */
inline QWidget *task(const QString &icon, const QString &title, const QString &desc, std::function<void()> action)
{
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(16, 0, 0, 0);
    h->setSpacing(10);
    auto *ic = new QLabel;
    ic->setPixmap(QIcon::fromTheme(icon).pixmap(24, 24));
    ic->setFixedSize(26, 26);
    h->addWidget(ic, 0, Qt::AlignTop);
    auto *v = new QVBoxLayout;
    v->setSpacing(2);
    v->addWidget(new Link(title, std::move(action)));
    v->addWidget(plain(desc));
    h->addLayout(v, 1);
    return w;
}

/* ---------------- network location (Windows 7 "Home / Work / Public network") ---------------- */

/** "home", "work" or "public" for a connection profile; a network you use at home is a home network by default */
inline QString networkLocation(const QString &uuid)
{
    return netStore().value(QStringLiteral("Locations/") + uuid, QStringLiteral("home")).toString();
}
inline QString locationName(const QString &loc)
{
    return loc == QLatin1String("work") ? tr7("Work network")
         : loc == QLatin1String("public") ? tr7("Public network") : tr7("Home network");
}
/** Windows 7 calls a wired network "Мережа" (the Wi-Fi one by its name) */
inline QString networkName(const QString &type, const QString &connection)
{
    return type == QLatin1String("wifi") ? connection : tr7("Network");
}

/** One option of the "Set Network Location" window: big icon, blue title, grey text; a light blue box under the mouse */
class LocationOption : public QWidget
{
public:
    LocationOption(const QString &icon, const QString &title, const QString &text, std::function<void()> act)
        : m_act(std::move(act))
    {
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(5, 5, 8, 5);
        h->setSpacing(6);
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(icon).pixmap(48, 48));
        h->addWidget(ic, 0, Qt::AlignTop);
        auto *v = new QVBoxLayout;
        v->setSpacing(0);
        auto *t = new QLabel(title);
        QFont f = t->font();
        f.setPointSizeF(f.pointSizeF() * 1.25);
        t->setFont(f);
        t->setStyleSheet(QStringLiteral("color: #1e3287;"));
        v->addWidget(t);
        auto *d = new QLabel(text);
        d->setWordWrap(true);
        d->setStyleSheet(QStringLiteral("color: #333333;"));
        v->addWidget(d);
        h->addLayout(v, 1);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!underMouse()) {
            return;
        }
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QColor(0xb9, 0xe4, 0xf8));
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, QColor(0xf4, 0xfb, 0xfe));
        g.setColorAt(1, QColor(0xe6, 0xf5, 0xfd));
        p.setBrush(g);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    }
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::HoverEnter || e->type() == QEvent::HoverLeave) {
            update();
        }
        return QWidget::event(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (rect().contains(e->position().toPoint())) {
            m_act();
        }
    }

private:
    std::function<void()> m_act;
};

/** Windows 7 "Set Network Location" (netcenter "Вибір розташування мережі") */
/** Called after the network location changed (set by win7sharing.h) */
inline std::function<void(QWidget *, const QString &)> &locationChanged()
{
    static std::function<void(QWidget *, const QString &)> f;
    return f;
}

inline void chooseNetworkLocation(QWidget *parent, const QString &uuid, const QString &netName, std::function<void()> done)
{
    // parented to the top window: the page that opened it is rebuilt after the choice
    auto *dlg = new QDialog(parent ? parent->window() : nullptr);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr7("Set Network Location"));
    dlg->setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-cpi-network")));

    auto *outer = new QVBoxLayout(dlg);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("locpage"));
    page->setAttribute(Qt::WA_StyledBackground, true);
    page->setStyleSheet(QStringLiteral("#locpage { background: white; }"));
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(32, 16, 32, 14);
    v->setSpacing(8);
    v->addWidget(blueTitle(tr7("Select a location for the '%1' network").arg(netName), 15));
    v->addWidget(plain(tr7("This computer is connected to a network. Windows will automatically apply the correct network settings based on the network's location.")));
    auto choose = [dlg, uuid, done](const QString &loc) {
        netStore().setValue(QStringLiteral("Locations/") + uuid, loc);
        netStore().sync();
        QWidget *win = dlg->parentWidget();
        dlg->accept();
        QTimer::singleShot(0, [win, loc, done] {
            if (locationChanged()) {
                locationChanged()(win, loc); // the profile's sharing settings come into effect (win7sharing.h)
            }
            done();
        });
    };
    v->addWidget(new LocationOption(QStringLiteral("seven-netloc-home"), tr7("Home network"),
                                    tr7("If all the computers on this network are at your home, and you recognize them, this is a trusted home network. Don't choose this for public places such as coffee shops or airports."),
                                    [choose] { choose(QStringLiteral("home")); }));
    v->addWidget(new LocationOption(QStringLiteral("seven-netloc-work"), tr7("Work network"),
                                    tr7("If all the computers on this network are at your workplace, and you recognize them, this is a trusted work network. Don't choose this for public places such as coffee shops or airports."),
                                    [choose] { choose(QStringLiteral("work")); }));
    v->addWidget(new LocationOption(QStringLiteral("seven-netloc-public"), tr7("Public network"),
                                    tr7("If you don't recognize all the computers on the network (for example, you're in a coffee shop or airport, or you have mobile broadband), this is a public network and is not trusted."),
                                    [choose] { choose(QStringLiteral("public")); }));
    // QCheckBox does not wrap: box + wrapping label that toggles it
    auto *alwaysRow = new QHBoxLayout;
    auto *always = new QCheckBox;
    always->setChecked(netStore().value(QStringLiteral("AlwaysPublic"), false).toBool());
    QObject::connect(always, &QCheckBox::toggled, [](bool on) {
        netStore().setValue(QStringLiteral("AlwaysPublic"), on);
    });
    auto *alwaysText = new QLabel(tr7("Treat all future networks that I connect to as public, and don't ask me again."));
    alwaysText->setWordWrap(true);
    alwaysText->setBuddy(always);
    alwaysRow->addWidget(always, 0, Qt::AlignTop);
    alwaysRow->addWidget(alwaysText, 1);
    v->addLayout(alwaysRow);
    v->addWidget(new Link(tr7("Help me choose"), [dlg] {
        QMessageBox::information(dlg, tr7("Set Network Location"),
                                 tr7("Home and work networks are trusted: computers on them can see each other and share files and printers.\n\nOn a public network your computer is hidden from others, and sharing is turned off."));
    }));
    outer->addWidget(page, 1);
    auto *bar = new QWidget;
    bar->setObjectName(QStringLiteral("locbar"));
    bar->setAttribute(Qt::WA_StyledBackground, true);
    bar->setStyleSheet(QStringLiteral("#locbar { background: #f0f0f0; border-top: 1px solid #dfdfdf; }"));
    auto *bh = new QHBoxLayout(bar);
    bh->setContentsMargins(10, 10, 12, 10);
    bh->addStretch(1);
    auto *cancel = new QPushButton(tr7("Cancel"));
    cancel->setMinimumWidth(75);
    QObject::connect(cancel, &QPushButton::clicked, dlg, &QDialog::reject);
    bh->addWidget(cancel);
    outer->addWidget(bar);
    // wrapped labels: size the window from the height its text needs at Windows 7's width
    outer->activate();
    dlg->setFixedSize(500, outer->hasHeightForWidth() ? outer->totalHeightForWidth(500) : dlg->sizeHint().height());
    dlg->show();
}

/** "Troubleshoot problems": Windows Network Diagnostics, in short */
inline void diagnoseNetwork(QWidget *parent)
{
    const QString c = nmcli({QStringLiteral("networking"), QStringLiteral("connectivity"), QStringLiteral("check")}).out.trimmed();
    QString msg;
    if (c == QLatin1String("full")) {
        msg = tr7("Troubleshooting couldn't identify the problem.\n\nThe computer is connected to the Internet.");
    } else if (c == QLatin1String("limited") || c == QLatin1String("portal")) {
        msg = tr7("Limited connectivity: the network works, but there is no Internet access.\n\nCheck your router or modem, and if this is the network of a coffee shop or hotel, open a browser and sign in on its page.");
    } else {
        msg = tr7("This computer is not connected to any network.\n\nPlug in a network cable or choose a wireless network by clicking the network icon in the notification area.");
    }
    QMessageBox::information(parent, tr7("Windows Network Diagnostics"), msg);
}

inline QWidget *buildCenterPage(const Nav &nav)
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

    // left pane
    auto *side = new QWidget;
    side->setObjectName(QStringLiteral("win7side"));
    side->setAttribute(Qt::WA_StyledBackground, true);
    side->setFixedWidth(200);
    auto *sv = new QVBoxLayout(side);
    sv->setContentsMargins(22, 16, 10, 14);
    sv->setSpacing(7);
    const QString sideColor = QStringLiteral("#1e3a5f");
    sv->addWidget(new Link(tr7("Control Panel Home"), nav.home, sideColor));
    sv->addSpacing(9);
    sv->addWidget(new Link(tr7("Change adapter settings"), [nav] {
        nav.open(ConnectionsView);
    }, sideColor));
    sv->addWidget(new Link(tr7("Change advanced sharing settings"), [nav] {
        nav.open(SharingView);
    }, sideColor));
    sv->addStretch(1);
    sv->addWidget(grey(tr7("See also")));
    sv->addWidget(new Link(tr7("HomeGroup"), [] {
        launch(QStringLiteral("dolphin"), {QStringLiteral("smb:/")});
    }, sideColor));
    sv->addWidget(new Link(tr7("Internet Options"), [] {
        launch(QStringLiteral("/usr/bin/kcmshell6"), {QStringLiteral("kcm_proxy")});
    }, sideColor));
    sv->addWidget(new Link(tr7("Windows Firewall", "link"), [] {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--firewall")});
    }, sideColor));
    h->addWidget(side);

    // main part
    auto *main = new QWidget;
    auto *mv = new QVBoxLayout(main);
    mv->setContentsMargins(20, 18, 24, 20);
    mv->setSpacing(0);
    auto *content = new QWidget;
    content->setMaximumWidth(660);
    auto *cv = new QVBoxLayout(content);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    cv->addWidget(blueTitle(tr7("View your basic network information and set up connections")));
    cv->addSpacing(14);

    // who is connected
    struct Active {
        QString name, uuid, iface, type, v4;
    };
    QList<Active> active;
    for (const QStringList &d : terse({QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("DEVICE,TYPE,STATE,CONNECTION,CON-UUID"), QStringLiteral("dev")})) {
        if (!d.value(2).startsWith(QLatin1String("connected")) || d.value(1) == QLatin1String("loopback") || d.value(1) == QLatin1String("bridge")
            || d.value(1) == QLatin1String("tun") || d.value(1) == QLatin1String("wifi-p2p")) {
            continue;
        }
        if (d.value(2).contains(QLatin1String("externally"))) {
            continue;
        }
        active.append({d.value(3), d.value(4), d.value(0), d.value(1), devValue(d.value(0), QStringLiteral("GENERAL.IP4-CONNECTIVITY"))});
    }
    const bool internet = nmcli({QStringLiteral("networking"), QStringLiteral("connectivity")}).out.trimmed() == QLatin1String("full");

    // mini map: this computer — network — Internet
    auto *map = new QHBoxLayout;
    map->setSpacing(0);
    map->addWidget(mapNode(QStringLiteral("seven-net-computer"), tr7("%1\n(This computer)").arg(QHostInfo::localHostName())));
    if (active.isEmpty()) {
        map->addWidget(mapLine(true), 0, Qt::AlignTop);
    } else {
        map->addWidget(mapLine(false), 0, Qt::AlignTop);
        map->addWidget(mapNode(active.size() == 1 ? QStringLiteral("seven-netloc-%1-map").arg(networkLocation(active.first().uuid)) : QStringLiteral("seven-net-network"),
                               active.size() == 1 ? networkName(active.first().type, active.first().name) : tr7("Multiple networks")));
        map->addWidget(mapLine(!internet), 0, Qt::AlignTop);
    }
    map->addWidget(mapNode(internet ? QStringLiteral("seven-net-internet") : QStringLiteral("seven-net-internet-off"), tr7("Internet")));
    map->addStretch(1);
    auto *fullMap = new Link(tr7("See full map"), [] {
        launch(QStringLiteral("dolphin"), {QStringLiteral("network:/")});
    });
    fullMap->setWordWrap(false);
    map->addWidget(fullMap, 0, Qt::AlignTop);
    cv->addLayout(map);
    cv->addSpacing(8);

    // active networks
    auto *activeHeader = new QHBoxLayout;
    activeHeader->addWidget(new SectionHeader(tr7("View your active networks")), 1);
    auto *connectLink = new Link(active.isEmpty() ? tr7("Connect to a network") : tr7("Connect or disconnect"), [] {
        openNetworkFlyout();
    });
    connectLink->setWordWrap(false);
    activeHeader->addWidget(connectLink);
    cv->addLayout(activeHeader);
    cv->addSpacing(6);
    if (active.isEmpty()) {
        auto *none = plain(tr7("You are currently not connected to any networks."));
        none->setContentsMargins(16, 0, 0, 0);
        cv->addWidget(none);
    }
    for (const Active &a : std::as_const(active)) {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(16, 0, 0, 0);
        const QString loc = networkLocation(a.uuid);
        const QString netName = networkName(a.type, a.name);
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(QStringLiteral("seven-netloc-%1-big").arg(loc)).pixmap(48, 48));
        row->addWidget(ic, 0, Qt::AlignTop);
        row->addSpacing(6);
        auto *nameCol = new QVBoxLayout;
        nameCol->setSpacing(1);
        auto *nameLabel = new QLabel(netName);
        QFont bf = nameLabel->font();
        bf.setBold(true);
        nameLabel->setFont(bf);
        nameCol->addWidget(nameLabel);
        QWidget *rootForLoc = root;
        const QString uuid = a.uuid;
        auto *locLink = new Link(locationName(loc), [rootForLoc, uuid, netName, nav] {
            chooseNetworkLocation(rootForLoc, uuid, netName, [nav] {
                nav.open(CenterView); // rebuild the page with the new icon
            });
        });
        locLink->setWordWrap(false);
        nameCol->addWidget(locLink);
        nameCol->addStretch(1);
        row->addLayout(nameCol, 1);
        auto *rule = new QWidget;
        rule->setAttribute(Qt::WA_StyledBackground, true);
        rule->setStyleSheet(QStringLiteral("background: #d5d5d5;"));
        rule->setFixedSize(1, 64);
        row->addWidget(rule, 0, Qt::AlignTop);
        row->addSpacing(10);
        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(4);
        grid->addWidget(grey(tr7("Access type:")), 0, 0);
        grid->addWidget(new QLabel(connectivityText(a.v4)), 0, 1);
        grid->addWidget(grey(tr7("Connections:")), 1, 0);
        Tile t;
        t.kind = a.type == QLatin1String("wifi") ? Tile::Wifi : a.type == QLatin1String("ethernet") ? Tile::Ethernet : a.type == QLatin1String("ppp") ? Tile::Dialup : Tile::Vpn;
        t.iface = a.iface;
        t.uuid = a.uuid;
        t.name = t.kind == Tile::Wifi ? tr7("Wireless Network Connection") : a.name;
        t.active = true;
        t.description = productOf(a.iface);
        auto *conRow = new QHBoxLayout;
        auto *conIcon = new QLabel;
        conIcon->setPixmap(QIcon::fromTheme(t.kind == Tile::Wifi ? QStringLiteral("seven-net-bars5") : QStringLiteral("seven-net-plug")).pixmap(16, 16));
        conRow->addWidget(conIcon);
        QWidget *rootPtr = root;
        conRow->addWidget(new Link(t.name, [rootPtr, t] {
            showStatus(rootPtr, t, [] {});
        }));
        conRow->addStretch(1);
        grid->addLayout(conRow, 1, 1);
        grid->setRowStretch(2, 1);
        row->addLayout(grid);
        cv->addLayout(row);
        cv->addSpacing(6);
    }
    cv->addSpacing(10);

    // change settings
    cv->addWidget(new SectionHeader(tr7("Change your networking settings")));
    cv->addSpacing(8);
    cv->addWidget(task(QStringLiteral("seven-net-new"), tr7("Set up a new connection or network"),
                       hasPptp() ? tr7("Set up a broadband (PPPoE) or VPN connection.")
                                 : tr7("Set up a broadband (PPPoE) connection."),
                       [root] {
                           newConnectionWizard(root->window(), [] {});
                       }));
    cv->addSpacing(12);
    cv->addWidget(task(QStringLiteral("seven-net-connect"), tr7("Connect to a network", "task"),
                       tr7("Connect or reconnect to a wireless, wired or VPN network connection."), [] {
                           openNetworkFlyout();
                       }));
    cv->addSpacing(12);
    cv->addWidget(task(QStringLiteral("seven-hg"), tr7("Choose homegroup and sharing options", "task"),
                       tr7("Access files and printers located on other network computers."), [] {
                           launch(QStringLiteral("dolphin"), {QStringLiteral("smb:/")});
                       }));
    cv->addSpacing(12);
    cv->addWidget(task(QStringLiteral("seven-net-troubleshoot"), tr7("Troubleshoot problems"),
                       tr7("Diagnose and repair network problems, or get troubleshooting information."), [root] {
                           diagnoseNetwork(root->window());
                       }));
    mv->addWidget(content);
    mv->addStretch(1);
    h->addWidget(main, 1);
    return root;
}

/** Tile of an adapter or connection by interface name or UUID (for desktop shortcuts) */
inline std::optional<Tile> findTile(const QString &key)
{
    for (const Tile &t : loadTiles()) {
        if (t.iface == key || t.uuid == key) {
            return t;
        }
    }
    return std::nullopt;
}
}
