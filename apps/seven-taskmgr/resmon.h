/*
 * Windows 7 "Resource Monitor" (resmon.exe, layout from wdc.dll UIFILE 30000-30005):
 * Overview / CPU / Memory / Disk / Network tabs, collapsible sections on the left, graphs on the right.
 *
 *   processes, CPU, memory, hard faults   -> /proc
 *   per-process disk I/O                   -> /proc/<pid>/io (own processes; others need root)
 *   disks, queue length, active time       -> /proc/diskstats, df
 *   services                               -> systemd units and their cgroups (cpu.stat)
 *   handles / modules                      -> /proc/<pid>/fd, /proc/<pid>/maps
 *   TCP connections, latency, traffic      -> ss -tinp (per-process traffic = TCP bytes)
 *   listening ports                        -> ss -tulnp, firewall state from /etc/ufw/ufw.conf
 * Linux has no per-file disk activity, so "Disk Activity" lists processes instead of files.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"


namespace ResMon
{
/** "key: value" / "key=value" line of a /proc or config file */
inline QString keyValue(const QString &text, const QString &key, QChar sep)
{
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const int i = line.indexOf(sep);
        if (i > 0 && line.left(i).trimmed() == key) {
            return line.mid(i + 1).trimmed().remove(QLatin1Char('"'));
        }
    }
    return {};
}

/* ---------------- formatting ---------------- */

inline QString num(double v, int prec = 0)
{
    return QLocale{}.toString(v, 'f', prec);
}

inline QString group(qint64 v)
{
    return QLocale{}.toString(v);
}

/* ---------------- samples ---------------- */

struct Proc {
    int pid = 0;
    int ppid = 0;
    QString image;
    QString description;
    QString cmdline;
    char state = 'S';
    int threads = 0;
    quint64 ticks = 0;
    quint64 majflt = 0;
    qint64 rssKb = 0, anonKb = 0, fileKb = 0, shmemKb = 0, swapKb = 0;
    quint64 readBytes = 0, writeBytes = 0;
    bool ioKnown = false;
    int ioPriority = -1;
    // rates, filled from two samples
    double cpu = 0, cpuAvg = 0;
    double faults = 0;
    double readRate = 0, writeRate = 0;
    double sendRate = 0, recvRate = 0;

    QString key() const
    {
        return QString::number(pid);
    }
    QString status() const
    {
        return state == 'T' || state == 't' ? tr7("Paused") : state == 'Z' ? tr7("Not Responding") : tr7("Running", "service");
    }
};

/** exe name -> program name from the .desktop files, like the "Description" column */
inline const QHash<QString, QString> &descriptions()
{
    static QHash<QString, QString> map;
    static bool done = false;
    if (done) {
        return map;
    }
    done = true;
    QSet<QString> ambiguous;
    QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &d : dirs) {
        for (const QFileInfo &fi : QDir(d).entryInfoList({QStringLiteral("*.desktop")}, QDir::Files)) {
            const QString text = readFile(fi.filePath());
            QString name, exec;
            QHash<QString, QString> names; // Name and Name[xx]
            bool main = false;
            for (const QString &line : text.split(QLatin1Char('\n'))) {
                if (line.startsWith(QLatin1Char('['))) {
                    main = line.trimmed() == QLatin1String("[Desktop Entry]");
                    continue;
                }
                if (!main) {
                    continue;
                }
                if (line.startsWith(QLatin1String("Name")) && line.contains(QLatin1Char('='))) {
                    names.insert(line.section(QLatin1Char('='), 0, 0).trimmed(), line.section(QLatin1Char('='), 1).trimmed());
                } else if (line.startsWith(QLatin1String("Exec="))) {
                    exec = line.mid(5).trimmed();
                }
            }
            // the name in the UI language (Name[uk] only when the UI is Ukrainian)
            for (const QString &x : Win7I18n::keySuffixes()) {
                if (name.isEmpty()) {
                    name = names.value(QStringLiteral("Name[%1]").arg(x));
                }
            }
            if (name.isEmpty()) {
                name = names.value(QStringLiteral("Name"));
            }
            QString bin = exec.section(QLatin1Char(' '), 0, 0);
            if (bin == QLatin1String("env")) {
                bin = exec.section(QLatin1Char(' '), 1, 1).contains(QLatin1Char('=')) ? exec.section(QLatin1Char(' '), 2, 2) : exec.section(QLatin1Char(' '), 1, 1);
            }
            bin = QFileInfo(bin.remove(QLatin1Char('"'))).fileName();
            if (bin.isEmpty() || name.isEmpty()) {
                continue;
            }
            // one program behind many shortcuts (wine, seven-control, ...) has no single description
            if (map.contains(bin) && map.value(bin) != name) {
                ambiguous.insert(bin);
            }
            map.insert(bin, name);
        }
    }
    for (const QString &b : std::as_const(ambiguous)) {
        map.remove(b);
    }
    for (const char *host : {"env", "sh", "bash", "python3", "wine", "flatpak", "snap", "java", "steam"}) {
        map.remove(QLatin1String(host));
    }
    return map;
}

inline QList<Proc> readProcs()
{
    QList<Proc> list;
    const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &p : pids) {
        bool ok = false;
        const int pid = p.toInt(&ok);
        if (!ok) {
            continue;
        }
        const QString base = QStringLiteral("/proc/") + p + QLatin1Char('/');
        const QString stat = readFile(base + QStringLiteral("stat"));
        const int rp = stat.lastIndexOf(QLatin1Char(')'));
        if (rp < 0) {
            continue;
        }
        const QStringList f = stat.mid(rp + 2).split(QLatin1Char(' '));
        Proc pr;
        pr.pid = pid;
        pr.state = f.value(0).isEmpty() ? 'S' : f.value(0).at(0).toLatin1();
        pr.ppid = f.value(1).toInt();
        if (pr.ppid == 2 || pid == 2) {
            continue; // kernel threads
        }
        pr.majflt = f.value(9).toULongLong();
        pr.ticks = f.value(11).toULongLong() + f.value(12).toULongLong();
        pr.threads = f.value(17).toInt();
        const QString comm = stat.mid(stat.indexOf(QLatin1Char('(')) + 1, rp - stat.indexOf(QLatin1Char('(')) - 1);
        QByteArray cmd;
        {
            QFile c(base + QStringLiteral("cmdline"));
            if (c.open(QIODevice::ReadOnly)) {
                cmd = c.readAll();
            }
        }
        const QList<QByteArray> argv = cmd.split('\0');
        pr.cmdline = QString::fromUtf8(cmd).replace(QLatin1Char('\0'), QLatin1Char(' ')).trimmed();
        QString exe = QFileInfo(base + QStringLiteral("exe")).symLinkTarget();
        QString image = QFileInfo(exe).fileName();
        if (image.isEmpty() && !argv.isEmpty()) {
            image = QFileInfo(QString::fromUtf8(argv.first()).section(QLatin1Char(' '), 0, 0)).fileName();
        }
        // interpreters: show the script, like Windows shows the program and not the host
        if ((image.startsWith(QLatin1String("python")) || image == QLatin1String("bash") || image == QLatin1String("sh") || image == QLatin1String("perl"))
            && argv.size() > 1 && !argv.at(1).startsWith('-') && !argv.at(1).isEmpty()) {
            image = QFileInfo(QString::fromUtf8(argv.at(1))).fileName();
        }
        if (image.isEmpty()) {
            image = comm;
        }
        pr.image = image;
        pr.description = descriptions().value(image);
        const QString status = readFile(base + QStringLiteral("status"));
        auto kb = [&status](const char *key) {
            const int i = status.indexOf(QLatin1String(key));
            return i < 0 ? 0 : status.mid(i + int(strlen(key))).trimmed().section(QLatin1Char(' '), 0, 0).toLongLong();
        };
        pr.rssKb = kb("VmRSS:");
        pr.anonKb = kb("RssAnon:");
        pr.fileKb = kb("RssFile:");
        pr.shmemKb = kb("RssShmem:");
        pr.swapKb = kb("VmSwap:");
        const QString io = readFile(base + QStringLiteral("io"));
        if (!io.isEmpty()) {
            pr.ioKnown = true;
            pr.readBytes = keyValue(io, QStringLiteral("read_bytes"), QLatin1Char(':')).toULongLong();
            pr.writeBytes = keyValue(io, QStringLiteral("write_bytes"), QLatin1Char(':')).toULongLong();
        }
        list.append(pr);
    }
    return list;
}

/** ioprio_get: "Висока" / "Звичайна" / "Низька" / "Фонова" like the I/O Priority column */
inline QString ioPriority(int pid)
{
    const long v = syscall(SYS_ioprio_get, 1 /* IOPRIO_WHO_PROCESS */, pid);
    if (v < 0) {
        return {};
    }
    const int cls = int(v >> 13);
    const int data = int(v & 0xff);
    if (cls == 1) {
        return tr7("High", "speed");
    }
    if (cls == 3) {
        return tr7("Background");
    }
    if (cls == 2 && data >= 6) {
        return tr7("Low", "speed");
    }
    return tr7("Normal", "speed");
}

struct DiskStat {
    QString name;
    quint64 readSectors = 0, writeSectors = 0, ioTicks = 0, weightedTicks = 0;
};

/** Whole physical disks (no partitions), never the Windows NVMe the user keeps away from Linux */
inline QList<DiskStat> readDisks()
{
    QList<DiskStat> list;
    for (const QString &line : readFile(QStringLiteral("/proc/diskstats")).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList f = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (f.size() < 14) {
            continue;
        }
        const QString name = f.at(2);
        if (!QFileInfo::exists(QStringLiteral("/sys/block/") + name + QStringLiteral("/device")) || name.startsWith(QLatin1String("nvme"))) {
            continue;
        }
        DiskStat d;
        d.name = name;
        d.readSectors = f.at(5).toULongLong();
        d.writeSectors = f.at(9).toULongLong();
        d.ioTicks = f.at(12).toULongLong();
        d.weightedTicks = f.at(13).toULongLong();
        list.append(d);
    }
    return list;
}

struct NetDev {
    QString name;
    quint64 rx = 0, tx = 0;
    double speedMbit = 0;
};

inline QList<NetDev> readNet()
{
    QList<NetDev> list;
    const QStringList lines = readFile(QStringLiteral("/proc/net/dev")).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = 2; i < lines.size(); ++i) {
        const QString name = lines.at(i).section(QLatin1Char(':'), 0, 0).trimmed();
        if (name == QLatin1String("lo") || name.startsWith(QLatin1String("docker")) || name.startsWith(QLatin1String("veth"))) {
            continue;
        }
        if (readFile(QStringLiteral("/sys/class/net/%1/operstate").arg(name)).trimmed() != QLatin1String("up")) {
            continue;
        }
        const QStringList f = lines.at(i).section(QLatin1Char(':'), 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        NetDev d;
        d.name = name;
        d.rx = f.value(0).toULongLong();
        d.tx = f.value(8).toULongLong();
        d.speedMbit = readFile(QStringLiteral("/sys/class/net/%1/speed").arg(name)).trimmed().toDouble();
        if (d.speedMbit <= 0) {
            d.speedMbit = QFileInfo::exists(QStringLiteral("/sys/class/net/%1/wireless").arg(name)) ? 300 : 1000;
        }
        list.append(d);
    }
    return list;
}

struct Socket {
    QString key; // local->peer
    int pid = 0;
    QString image;
    QString local, localPort, peer, peerPort;
    double rtt = -1;
    double loss = 0;
    quint64 sent = 0, received = 0;
    double sendRate = 0, recvRate = 0;
};

inline void splitAddress(const QString &a, QString &host, QString &port)
{
    const int c = a.lastIndexOf(QLatin1Char(':'));
    host = a.left(c);
    port = a.mid(c + 1);
    if (host.startsWith(QLatin1Char('[')) && host.endsWith(QLatin1Char(']'))) {
        host = host.mid(1, host.size() - 2);
    }
    if (host.startsWith(QLatin1String("::ffff:"))) {
        host = host.mid(7);
    }
    host.remove(QRegularExpression(QStringLiteral("%.*$")));
}

inline QList<Socket> readTcp()
{
    QList<Socket> list;
    QProcess p;
    p.start(QStringLiteral("ss"), {QStringLiteral("-tinpH"), QStringLiteral("state"), QStringLiteral("established")});
    p.waitForFinished(3000);
    const QStringList lines = QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'));
    static const QRegularExpression user(QStringLiteral("\\(\\(\"([^\"]+)\",pid=(\\d+)"));
    static const QRegularExpression rtt(QStringLiteral("\\brtt:([0-9.]+)"));
    static const QRegularExpression sent(QStringLiteral("bytes_sent:(\\d+)"));
    static const QRegularExpression acked(QStringLiteral("bytes_acked:(\\d+)"));
    static const QRegularExpression recv(QStringLiteral("bytes_received:(\\d+)"));
    static const QRegularExpression retrans(QStringLiteral("retrans:\\d+/(\\d+)"));
    static const QRegularExpression segs(QStringLiteral("segs_out:(\\d+)"));
    Socket *cur = nullptr;
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty()) {
            continue;
        }
        if (!line.startsWith(QLatin1Char('\t')) && !line.startsWith(QLatin1Char(' '))) {
            const QStringList f = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (f.size() < 4) {
                cur = nullptr;
                continue;
            }
            Socket s;
            splitAddress(f.at(2), s.local, s.localPort);
            splitAddress(f.at(3), s.peer, s.peerPort);
            s.key = f.at(2) + QStringLiteral("->") + f.at(3);
            const auto m = user.match(line);
            if (m.hasMatch()) {
                s.image = m.captured(1);
                s.pid = m.captured(2).toInt();
            }
            list.append(s);
            cur = &list.last();
        } else if (cur) {
            auto get = [&line](const QRegularExpression &re) {
                const auto m = re.match(line);
                return m.hasMatch() ? m.captured(1) : QString();
            };
            if (!get(rtt).isEmpty()) {
                cur->rtt = get(rtt).toDouble();
            }
            cur->sent = (get(acked).isEmpty() ? get(sent) : get(acked)).toULongLong();
            cur->received = get(recv).toULongLong();
            const double out = get(segs).toDouble();
            cur->loss = out > 0 ? get(retrans).toDouble() * 100.0 / out : 0;
        }
    }
    return list;
}

struct Listener {
    QString image;
    int pid = 0;
    QString address, port, protocol;
};

inline QList<Listener> readListeners()
{
    QList<Listener> list;
    QProcess p;
    p.start(QStringLiteral("ss"), {QStringLiteral("-tulnpH")});
    p.waitForFinished(3000);
    static const QRegularExpression user(QStringLiteral("\\(\\(\"([^\"]+)\",pid=(\\d+)"));
    for (const QString &line : QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList f = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (f.size() < 5) {
            continue;
        }
        Listener l;
        l.protocol = f.at(0).toUpper();
        QString host;
        splitAddress(f.at(4), host, l.port);
        l.address = host.contains(QLatin1Char(':')) || host == QLatin1String("*") && f.at(4).startsWith(QLatin1Char('['))
            ? QStringLiteral("IPv6 ") + (host == QLatin1String("::") || host == QLatin1String("*") ? tr7("not specified") : host)
            : QStringLiteral("IPv4 ") + (host == QLatin1String("0.0.0.0") || host == QLatin1String("*") ? tr7("not specified") : host);
        const auto m = user.match(line);
        if (m.hasMatch()) {
            l.image = m.captured(1);
            l.pid = m.captured(2).toInt();
        } else {
            l.image = tr7("System");
        }
        list.append(l);
    }
    return list;
}

struct Service {
    QString name;
    QString description;
    int pid = 0;
    bool running = false;
    QString group;
    QString cgroup;
    quint64 usageUsec = 0;
    double cpu = 0, cpuAvg = 0;
};

inline QList<Service> readServices()
{
    QList<Service> list;
    QProcess p;
    p.start(QStringLiteral("systemctl"), {QStringLiteral("show"), QStringLiteral("--type=service"), QStringLiteral("--all"),
                                          QStringLiteral("-p"), QStringLiteral("Id,Description,MainPID,ActiveState,Slice,ControlGroup,LoadState"), QStringLiteral("*")});
    p.waitForFinished(5000);
    for (const QString &block : QString::fromUtf8(p.readAllStandardOutput()).split(QStringLiteral("\n\n"), Qt::SkipEmptyParts)) {
        Service s;
        QString load;
        for (const QString &line : block.split(QLatin1Char('\n'))) {
            const QString k = line.section(QLatin1Char('='), 0, 0);
            const QString v = line.section(QLatin1Char('='), 1);
            if (k == QLatin1String("Id")) {
                s.name = v.endsWith(QLatin1String(".service")) ? v.chopped(8) : v;
            } else if (k == QLatin1String("Description")) {
                s.description = v;
            } else if (k == QLatin1String("MainPID")) {
                s.pid = v.toInt();
            } else if (k == QLatin1String("ActiveState")) {
                s.running = v == QLatin1String("active") || v == QLatin1String("reloading");
            } else if (k == QLatin1String("Slice")) {
                s.group = v.endsWith(QLatin1String(".slice")) ? v.chopped(6) : v;
            } else if (k == QLatin1String("ControlGroup")) {
                s.cgroup = v;
            } else if (k == QLatin1String("LoadState")) {
                load = v;
            }
        }
        // user@UID is the whole desktop session, not a service in the Windows sense
        if (s.name.isEmpty() || load != QLatin1String("loaded") || s.name.contains(QLatin1Char('@')) && !s.running || s.name.startsWith(QLatin1String("user@"))
            || s.name.startsWith(QLatin1String("user-runtime-dir@"))) {
            continue;
        }
        if (!s.cgroup.isEmpty()) {
            const QString st = readFile(QStringLiteral("/sys/fs/cgroup") + s.cgroup + QStringLiteral("/cpu.stat"));
            s.usageUsec = keyValue(st, QStringLiteral("usage_usec"), QLatin1Char(' ')).toULongLong();
        }
        list.append(s);
    }
    return list;
}

/* ---------------- tables ---------------- */

/** A list that keeps its rows (selection, checks, scroll) while the values change every second */
class Table : public QWidget
{
public:
    struct Cell {
        QString text;
        double sort = 0;
        bool numeric = false;
    };
    struct Row {
        QString key;
        QList<Cell> cells;
    };

    /** `stretch`: the column that takes the free width, like Description / File / Address in Windows */
    Table(const QStringList &columns, const QList<int> &widths, bool checkable, int stretch = -1, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_checkable(checkable)
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        m_model = new QStandardItemModel(0, columns.size(), this);
        QStringList labels = columns;
        if (checkable) {
            labels[0] = QStringLiteral("      ") + labels[0]; // room for the "check all" box
        }
        m_model->setHorizontalHeaderLabels(labels);
        m_view = makeList(this);
        m_view->setStyleSheet(m_view->styleSheet() + QStringLiteral("QTreeView { border: none; }"));
        m_view->setModel(m_model);
        for (int i = 0; i < widths.size(); ++i) {
            m_view->setColumnWidth(i, widths.at(i));
        }
        for (int i = 1; i < columns.size(); ++i) {
            m_model->horizontalHeaderItem(i)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        }
        if (stretch >= 0) {
            m_view->header()->setSectionResizeMode(stretch, QHeaderView::Stretch);
        }
        v->addWidget(m_view);
        m_placeholder = new QLabel(m_view->viewport());
        m_placeholder->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        m_placeholder->hide();
        if (m_checkable) {
            m_all = new QCheckBox(m_view->header()->viewport());
            m_all->move(5, (m_view->header()->sizeHint().height() - 13) / 2);
            m_all->setToolTip(tr7("Select all"));
            connect(m_all, &QCheckBox::clicked, this, [this](bool on) {
                QSet<QString> keys;
                if (on) {
                    for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it) {
                        keys.insert(it.key());
                    }
                }
                setChecked(keys);
                if (checkedChanged) {
                    checkedChanged();
                }
            });
        }
        if (m_checkable) {
            connect(m_model, &QStandardItemModel::itemChanged, this, [this](QStandardItem *it) {
                if (it->column() != 0 || m_updating) {
                    return;
                }
                const QString k = it->data(Qt::UserRole).toString();
                if (it->checkState() == Qt::Checked) {
                    m_checked.insert(k);
                } else {
                    m_checked.remove(k);
                }
                if (checkedChanged) {
                    checkedChanged();
                }
            });
        }
    }

    std::function<void()> checkedChanged;
    std::function<void(const QString &key, QPoint)> menuRequested;

    QTreeView *view() const
    {
        return m_view;
    }
    const QSet<QString> &checked() const
    {
        return m_checked;
    }
    void setChecked(const QSet<QString> &keys)
    {
        m_checked = keys;
        m_updating = true;
        for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it) {
            it.value()->setCheckState(keys.contains(it.key()) ? Qt::Checked : Qt::Unchecked);
        }
        m_updating = false;
    }
    /** Grey hint shown while the list is empty ("select a process to see results") */
    void setPlaceholder(const QString &text)
    {
        m_placeholderText = text;
        updatePlaceholder();
    }
    QString rowText(const QString &key, int column) const
    {
        const auto it = m_rows.find(key);
        return it == m_rows.end() ? QString() : m_model->item(it.value()->row(), column)->text();
    }

    void update(const QList<Row> &rows)
    {
        m_updating = true;
        QSet<QString> seen;
        for (const Row &r : rows) {
            seen.insert(r.key);
            QStandardItem *first = m_rows.value(r.key);
            if (!first) {
                QList<QStandardItem *> items;
                for (int c = 0; c < m_model->columnCount(); ++c) {
                    auto *it = new NumItem(QString(), 0);
                    it->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                    if (c == 0) {
                        it->setData(r.key, Qt::UserRole);
                        if (m_checkable) {
                            it->setCheckable(true);
                            it->setCheckState(m_checked.contains(r.key) ? Qt::Checked : Qt::Unchecked);
                        }
                    }
                    items << it;
                }
                m_model->appendRow(items);
                first = items.first();
                m_rows.insert(r.key, first);
            }
            const int row = first->row();
            for (int c = 0; c < r.cells.size() && c < m_model->columnCount(); ++c) {
                QStandardItem *it = m_model->item(row, c);
                if (it->text() != r.cells.at(c).text) {
                    it->setText(r.cells.at(c).text);
                }
                it->setData(r.cells.at(c).numeric ? QVariant(r.cells.at(c).sort) : QVariant(r.cells.at(c).text.toLower()), Qt::UserRole + 1);
                if (r.cells.at(c).numeric) {
                    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                }
            }
        }
        for (auto it = m_rows.begin(); it != m_rows.end();) {
            if (!seen.contains(it.key())) {
                m_model->removeRow(it.value()->row());
                m_checked.remove(it.key());
                it = m_rows.erase(it);
            } else {
                ++it;
            }
        }
        if (m_view->header()->sortIndicatorSection() >= 0) {
            m_model->setSortRole(Qt::UserRole + 1);
            m_model->sort(m_view->header()->sortIndicatorSection(), m_view->header()->sortIndicatorOrder());
        }
        m_updating = false;
        updatePlaceholder();
    }

    void sortBy(int column, Qt::SortOrder order)
    {
        m_model->setSortRole(Qt::UserRole + 1);
        m_view->sortByColumn(column, order);
    }

private:
    void updatePlaceholder()
    {
        const bool show = !m_placeholderText.isEmpty() && m_model->rowCount() == 0;
        m_placeholder->setVisible(show);
        if (show) {
            m_placeholder->setText(m_placeholderText);
            m_placeholder->setGeometry(0, 6, m_view->viewport()->width(), 20);
        }
    }

    QStandardItemModel *m_model;
    QTreeView *m_view;
    QLabel *m_placeholder = nullptr;
    QString m_placeholderText;
    QCheckBox *m_all = nullptr;
    bool m_checkable;
    bool m_updating = false;
    QHash<QString, QStandardItem *> m_rows;
    QSet<QString> m_checked;
};

/* ---------------- section ---------------- */

/** Round button with a chevron, used to collapse sections and the graph pane */
class RoundButton : public QAbstractButton
{
public:
    enum Dir { Up, Down, Right, Left };
    explicit RoundButton(Dir d, QWidget *parent = nullptr)
        : QAbstractButton(parent)
        , dir(d)
    {
        setFixedSize(20, 20);
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
    }
    Dir dir;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        const bool hot = underMouse();
        g.setColorAt(0, hot ? QColor(0xf2, 0xf9, 0xff) : QColor(0xff, 0xff, 0xff));
        g.setColorAt(1, hot ? QColor(0xc8, 0xe4, 0xf8) : QColor(0xe2, 0xe2, 0xe2));
        p.setBrush(g);
        p.setPen(QPen(hot ? QColor(0x3c, 0x7f, 0xb1) : QColor(0x70, 0x70, 0x70), 1));
        p.drawEllipse(r);
        p.setPen(QPen(QColor(0x20, 0x20, 0x20), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF c = r.center();
        QPolygonF chevron;
        switch (dir) {
        case Up:
            chevron << c + QPointF(-3.5, 2) << c + QPointF(0, -1.5) << c + QPointF(3.5, 2);
            break;
        case Down:
            chevron << c + QPointF(-3.5, -1.5) << c + QPointF(0, 2) << c + QPointF(3.5, -1.5);
            break;
        case Right:
            chevron << c + QPointF(-1.5, -3.5) << c + QPointF(2, 0) << c + QPointF(-1.5, 3.5);
            break;
        case Left:
            chevron << c + QPointF(1.5, -3.5) << c + QPointF(-2, 0) << c + QPointF(1.5, 3.5);
            break;
        }
        p.drawPolyline(chevron);
    }
};

/** "■ 21% CPU Usage" in a section header */
class Indicator : public QWidget
{
public:
    explicit Indicator(const QColor &color, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_color(color)
    {
        setMinimumWidth(40);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setFixedHeight(18);
    }
    void setText(const QString &t)
    {
        if (t != m_text) {
            m_text = t;
            setToolTip(t);
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(QRect(0, 3, 12, 12), Qt::black);
        p.fillRect(QRect(1, 4, 10, 10), m_color);
        p.setPen(Qt::black);
        p.drawText(QRect(18, 0, width() - 18, height()), Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(m_text, Qt::ElideRight, width() - 18));
    }

private:
    QColor m_color;
    QString m_text;
};

/** Grey header of a section: title, two indicators at fixed places, extra widgets and the round button */
class HeaderBar : public QWidget
{
public:
    explicit HeaderBar(const QString &title, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("resmonheader"));
        setAttribute(Qt::WA_StyledBackground, true);
        setFixedHeight(32);
        setStyleSheet(QStringLiteral("#resmonheader { border: 1px solid #a5a5a5; border-bottom-color: #8e8e8e;"
                                     " background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fbfbfb, stop:0.5 #eaeaea, stop:0.51 #dedede, stop:1 #d2d2d2); }"));
        m_title = new QLabel(title, this);
        QFont f = m_title->font();
        f.setBold(true);
        f.setPixelSize(15);
        m_title->setFont(f);
        m_title->setStyleSheet(QStringLiteral("background: transparent;"));
        toggle = new RoundButton(RoundButton::Up, this);
    }
    RoundButton *toggle;
    QList<Indicator *> indicators;
    QList<QWidget *> extras; // right-aligned, before the round button

protected:
    void resizeEvent(QResizeEvent *) override
    {
        const int h = height();
        toggle->move(width() - toggle->width() - 8, (h - toggle->height()) / 2);
        int right = toggle->x() - 6;
        for (QWidget *w : std::as_const(extras)) {
            const int ww = qMin(w->sizeHint().width() < 60 ? w->sizeHint().width() : 230, right - 200);
            w->setGeometry(right - ww, (h - 24) / 2, ww, 24);
            right -= ww + 4;
        }
        // Windows puts the indicators at about 28 % and 63 % of the header
        const int x1 = qMax(m_title->sizeHint().width() + 16, int(width() * 0.28));
        const int x2 = qMax(x1 + 120, int(width() * 0.63));
        m_title->setGeometry(8, 0, qMin(m_title->sizeHint().width(), (indicators.isEmpty() ? right : x1) - 12), h);
        if (indicators.size() > 0) {
            indicators.at(0)->setGeometry(x1, (h - 18) / 2, (indicators.size() > 1 ? x2 : right) - x1 - 8, 18);
        }
        if (indicators.size() > 1) {
            indicators.at(1)->setGeometry(x2, (h - 18) / 2, right - x2 - 4, 18);
        }
    }

private:
    QLabel *m_title;
};

class Section : public QWidget
{
public:
    Section(const QString &title, QWidget *body, int bodyHeight, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_body(body)
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 6);
        v->setSpacing(0);
        m_header = new HeaderBar(title);
        v->addWidget(m_header);
        m_frame = new QWidget;
        m_frame->setObjectName(QStringLiteral("resmonbody"));
        m_frame->setAttribute(Qt::WA_StyledBackground, true);
        m_frame->setStyleSheet(QStringLiteral("#resmonbody { border: 1px solid #a5a5a5; border-top: none; background: white; }"));
        auto *fv = new QVBoxLayout(m_frame);
        fv->setContentsMargins(1, 0, 1, 1);
        fv->setSpacing(0);
        m_filter = new QLabel;
        m_filter->setStyleSheet(QStringLiteral("QLabel { padding: 4px 8px; color: black;"
                                               " background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fdb64e, stop:0.5 #fa9e2c, stop:1 #f69220); }"));
        m_filter->hide();
        fv->addWidget(m_filter);
        fv->addWidget(m_body, 1);
        m_frame->setFixedHeight(bodyHeight);
        v->addWidget(m_frame);
        connect(m_header->toggle, &QAbstractButton::clicked, this, [this] {
            setExpanded(!m_frame->isVisible());
        });
    }

    Indicator *addIndicator(const QColor &c)
    {
        auto *i = new Indicator(c, m_header);
        m_header->indicators << i;
        return i;
    }
    void addHeaderWidget(QWidget *w)
    {
        w->setParent(m_header);
        m_header->extras << w;
    }
    void setExpanded(bool on)
    {
        m_frame->setVisible(on);
        m_header->toggle->dir = on ? RoundButton::Up : RoundButton::Down;
        m_header->toggle->update();
    }
    void setFilter(const QString &text)
    {
        m_filter->setVisible(!text.isEmpty());
        m_filter->setText(text);
    }
    QWidget *header() const
    {
        return m_header;
    }

private:
    HeaderBar *m_header;
    QWidget *m_frame;
    QWidget *m_body;
    QLabel *m_filter;
};

/* ---------------- graphs ---------------- */

class ResGraph : public QWidget
{
public:
    ResGraph(const QString &title, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_title(title)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setGraphHeight(100);
    }
    void setGraphHeight(int h)
    {
        m_graphH = h;
        setFixedHeight(h + 34);
        update();
    }
    void setTitle(const QString &t)
    {
        m_title = t;
        update();
    }
    void setScale(const QString &top, const QString &bottom)
    {
        m_top = top;
        m_bottom = bottom;
    }
    /** green filled series (0..1), blue line (0..1, <0 = none), orange line for the checked processes */
    void add(double fill, double line = -1, double orange = -1)
    {
        m_fill.append(qBound(0.0, fill, 1.0));
        m_line.append(line < 0 ? -1 : qBound(0.0, line, 1.0));
        m_orange.append(orange < 0 ? -1 : qBound(0.0, orange, 1.0));
        while (m_fill.size() > 61) {
            m_fill.removeFirst();
            m_line.removeFirst();
            m_orange.removeFirst();
        }
        m_scroll = (m_scroll + 1) % 10;
        update();
    }
    void rescale(double factor)
    {
        for (double &v : m_fill) {
            v = qBound(0.0, v * factor, 1.0);
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        p.setPen(Qt::black);
        const int labelsRight = width() - 14;
        const QRect top(4, 0, labelsRight - 4, 16);
        const int topW = fontMetrics().horizontalAdvance(m_top);
        p.drawText(top, Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(m_title, Qt::ElideRight, labelsRight - topW - 14));
        p.drawText(top, Qt::AlignRight | Qt::AlignVCenter, m_top);
        const QRect g(4, 17, width() - 20, m_graphH);
        // the bracket on the right that ties the scale labels to the graph
        p.setPen(QColor(0x40, 0x40, 0x40));
        const int bx = width() - 6;
        p.drawLine(bx - 4, 8, bx, 8);
        p.drawLine(bx, 8, bx, g.bottom() + 9);
        p.drawLine(bx - 4, g.bottom() + 9, bx, g.bottom() + 9);
        p.fillRect(g, Qt::black);
        // grid: 10 rows, columns every 10 seconds, scrolling
        p.setPen(QColor(0x00, 0x80, 0x40));
        for (int i = 1; i < 10; ++i) {
            const int y = g.top() + i * g.height() / 10;
            p.drawLine(g.left(), y, g.right(), y);
        }
        const qreal step = g.width() / 60.0;
        for (qreal x = g.right() - m_scroll * step; x > g.left(); x -= 10 * step) {
            p.drawLine(QPointF(x, g.top()), QPointF(x, g.bottom()));
        }
        p.setRenderHint(QPainter::Antialiasing);
        const int n = m_fill.size();
        if (n >= 2) {
            QPainterPath area;
            area.moveTo(g.right() - (n - 1) * step, g.bottom() + 0.5);
            QPainterPath edge;
            for (int i = 0; i < n; ++i) {
                const qreal x = g.right() - (n - 1 - i) * step;
                const qreal y = g.bottom() - m_fill.at(i) * (g.height() - 1);
                area.lineTo(x, y);
                i == 0 ? edge.moveTo(x, y) : edge.lineTo(x, y);
            }
            area.lineTo(g.right(), g.bottom() + 0.5);
            area.closeSubpath();
            p.save();
            p.setClipRect(g);
            p.fillPath(area, QColor(0x00, 0xb0, 0x00, 200));
            p.setPen(QPen(QColor(0x60, 0xff, 0x60), 1));
            p.drawPath(edge);
            auto drawLine = [&](const QList<double> &vals, const QColor &c) {
                QPainterPath path;
                bool started = false;
                for (int i = 0; i < vals.size(); ++i) {
                    if (vals.at(i) < 0) {
                        started = false;
                        continue;
                    }
                    const qreal x = g.right() - (vals.size() - 1 - i) * step;
                    const qreal y = g.bottom() - vals.at(i) * (g.height() - 2) - 1;
                    started ? path.lineTo(x, y) : path.moveTo(x, y);
                    started = true;
                }
                p.setPen(QPen(c, 2));
                p.drawPath(path);
            };
            drawLine(m_line, QColor(0x40, 0x60, 0xff));
            drawLine(m_orange, QColor(0xff, 0x8c, 0x00));
            p.restore();
        }
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(Qt::black);
        const QRect bottom(4, g.bottom() + 1, labelsRight - 4, 16);
        p.drawText(bottom, Qt::AlignLeft | Qt::AlignVCenter, m_showSeconds ? tr7("60 Seconds") : QString());
        p.drawText(bottom, Qt::AlignRight | Qt::AlignVCenter, m_bottom);
    }

public:
    bool m_showSeconds = true;

private:
    QString m_title;
    QString m_top = QStringLiteral("100%");
    QString m_bottom = QStringLiteral("0%");
    int m_graphH = 100;
    int m_scroll = 0;
    QList<double> m_fill, m_line, m_orange;
};

/** Steps of the automatic scale of the Disk and Network graphs */
inline double autoScale(double value, const QList<double> &steps)
{
    for (double s : steps) {
        if (value <= s) {
            return s;
        }
    }
    return steps.last();
}

/** Right pane: "▸" (hide graphs), "Views ▾" and the graphs of the current tab */
class GraphPane : public QWidget
{
public:
    explicit GraphPane(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(4, 4, 4, 0);
        v->setSpacing(4);
        auto *top = new QHBoxLayout;
        hide = new RoundButton(RoundButton::Right);
        hide->setToolTip(tr7("Hide Graphs"));
        top->addWidget(hide);
        top->addStretch(1);
        views = new QPushButton(tr7("Views"));
        auto *menu = new QMenu(views);
        auto *grp = new QActionGroup(menu);
        const QList<QPair<QString, int>> sizes{{tr7("Large"), 160}, {tr7("Medium"), 100}, {tr7("Small"), 60}};
        for (const auto &s : sizes) {
            QAction *a = menu->addAction(s.first);
            a->setCheckable(true);
            a->setChecked(s.second == 100);
            grp->addAction(a);
            const int hgt = s.second;
            connect(a, &QAction::triggered, this, [this, hgt] {
                m_height = hgt;
                for (ResGraph *g : std::as_const(m_all)) {
                    g->setGraphHeight(hgt);
                }
            });
        }
        views->setMenu(menu);
        views->setMinimumWidth(100);
        top->addWidget(views);
        v->addLayout(top);
        m_stack = new QStackedWidget;
        v->addWidget(m_stack, 1);
    }

    QWidget *addPage(const QList<ResGraph *> &graphs)
    {
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::StyledPanel);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *w = new QWidget;
        w->setObjectName(QStringLiteral("resmongraphs"));
        w->setAttribute(Qt::WA_StyledBackground, true);
        w->setStyleSheet(QStringLiteral("#resmongraphs { background: white; }"));
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(2, 4, 2, 4);
        v->setSpacing(6);
        for (ResGraph *g : graphs) {
            m_all << g;
            g->setGraphHeight(m_height);
            v->addWidget(g);
        }
        v->addStretch(1);
        scroll->setWidget(w);
        m_stack->addWidget(scroll);
        return scroll;
    }
    void setPage(int i)
    {
        m_stack->setCurrentIndex(i);
    }

    RoundButton *hide;
    QPushButton *views;

private:
    QStackedWidget *m_stack;
    QList<ResGraph *> m_all;
    int m_height = 100;
};

/* ---------------- physical memory bar (Memory tab) ---------------- */

struct MemInfo {
    qint64 installed = 0, total = 0, free = 0, available = 0, cached = 0, modified = 0, inUse = 0, standby = 0, reserved = 0;
    qint64 commit = 0, commitLimit = 0;
};

inline MemInfo readMem()
{
    MemInfo m;
    const QString t = readFile(QStringLiteral("/proc/meminfo"));
    auto kb = [&t](const char *k) {
        return meminfo(t, k);
    };
    m.total = kb("MemTotal");
    m.free = kb("MemFree");
    m.available = kb("MemAvailable");
    m.modified = kb("Dirty") + kb("Writeback");
    m.cached = kb("Cached") + kb("Buffers") + kb("SReclaimable");
    m.standby = qMax<qint64>(0, m.cached - m.modified);
    m.inUse = qMax<qint64>(0, m.total - m.free - m.cached);
    m.commit = kb("Committed_AS");
    m.commitLimit = kb("MemTotal") + kb("SwapTotal"); // Linux overcommits: compare with all memory, like Windows' commit limit
    // installed memory = online memory blocks
    const qint64 block = readFile(QStringLiteral("/sys/devices/system/memory/block_size_bytes")).trimmed().toLongLong(nullptr, 16);
    int blocks = 0;
    for (const QString &d : QDir(QStringLiteral("/sys/devices/system/memory")).entryList({QStringLiteral("memory*")}, QDir::Dirs)) {
        if (readFile(QStringLiteral("/sys/devices/system/memory/%1/online").arg(d)).trimmed() == QLatin1String("1")) {
            ++blocks;
        }
    }
    m.installed = block > 0 && blocks > 0 ? block * blocks / 1024 : m.total;
    m.reserved = qMax<qint64>(0, m.installed - m.total);
    return m;
}

class MemoryBar : public QWidget
{
public:
    explicit MemoryBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(190);
    }
    void setInfo(const MemInfo &m)
    {
        m_m = m;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        const QRect bar(10, 14, width() - 20, 30);
        const qint64 all = qMax<qint64>(1, m_m.installed);
        struct Part {
            qint64 kb;
            QColor color;
            QString name;
        };
        const QList<Part> parts{{m_m.reserved, QColor(0x9a, 0x9a, 0x9a), tr7("Hardware\nReserved")},
                                {m_m.inUse, QColor(0x8c, 0xb4, 0x1e), tr7("In Use")},
                                {m_m.modified, QColor(0xf0, 0x9c, 0x1e), tr7("Modified")},
                                {m_m.standby, QColor(0x4a, 0x86, 0xd6), tr7("Standby")},
                                {m_m.free, QColor(0xc4, 0xdc, 0xf6), tr7("Free")}};
        p.setPen(QColor(0x80, 0x80, 0x80));
        p.drawRect(bar.adjusted(0, 0, -1, -1));
        qreal x = bar.left() + 1;
        for (const Part &part : parts) {
            const qreal w = (bar.width() - 2) * double(part.kb) / all;
            p.fillRect(QRectF(x, bar.top() + 1, w, bar.height() - 2), part.color);
            x += w;
        }
        // legend
        const int lw = (width() - 120) / 5;
        for (int i = 0; i < parts.size(); ++i) {
            const int lx = 80 + i * lw;
            p.fillRect(QRect(lx, bar.bottom() + 10, 12, 12), Qt::black);
            p.fillRect(QRect(lx + 1, bar.bottom() + 11, 10, 10), parts.at(i).color);
            p.setPen(Qt::black);
            const QRect tr(lx + 16, bar.bottom() + 8, lw - 16, 60);
            p.drawText(tr, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, parts.at(i).name + QLatin1Char('\n') + tr7("%1 MB").arg(group(parts.at(i).kb / 1024)));
        }
        // totals
        const QList<QPair<QString, qint64>> totals{{tr7("Available"), m_m.available},
                                                  {tr7("Cached"), m_m.cached},
                                                  {tr7("Total", "memory"), m_m.total},
                                                  {tr7("Installed"), m_m.installed}};
        int y = bar.bottom() + 80;
        const int cx = width() * 2 / 5 + 40;
        for (const auto &t : totals) {
            p.drawText(QRect(cx - 100, y, 95, 16), Qt::AlignRight | Qt::AlignVCenter, t.first);
            p.drawText(QRect(cx + 5, y, 150, 16), Qt::AlignLeft | Qt::AlignVCenter, tr7("%1 MB").arg(group(t.second / 1024)));
            y += 16;
        }
    }

private:
    MemInfo m_m;
};

/* ---------------- the window ---------------- */

class ResourceMonitor : public QMainWindow
{
public:
    ResourceMonitor()
    {
        setWindowTitle(tr7("Resource Monitor"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-resmon")));
        resize(1600, 1000);
        m_ncpu = qMax(1, int(sysconf(_SC_NPROCESSORS_ONLN)));
        m_hz = qMax(1L, sysconf(_SC_CLK_TCK));

        auto *file = menuBar()->addMenu(tr7("&File"));
        file->addAction(tr7("E&xit"), this, &QWidget::close);
        auto *monitor = menuBar()->addMenu(tr7("&Monitor"));
        m_stop = monitor->addAction(tr7("&Stop Monitoring"), this, [this] {
            setMonitoring(false);
        });
        m_start = monitor->addAction(tr7("&Start Monitoring"), this, [this] {
            setMonitoring(true);
        });
        m_start->setEnabled(false);
        auto *help = menuBar()->addMenu(tr7("&Help"));
        help->addAction(tr7("&About"), this, [this] {
            QMessageBox about(this);
            about.setWindowTitle(tr7("About Resource Monitor"));
            about.setIconPixmap(QIcon::fromTheme(QStringLiteral("seven-resmon")).pixmap(48, 48));
            about.setText(tr7("<b>Resource Monitor</b>"));
            about.setInformativeText(tr7("Monitors the usage of the processor, memory, disks and network by the processes and services of the computer."));
            about.exec();
        });
        win7WrapMenuBar(this); // Windows 7: the menu bar wraps instead of showing a "»" arrow

        auto *central = new QWidget;
        auto *cv = new QVBoxLayout(central);
        cv->setContentsMargins(0, 2, 0, 0);
        cv->setSpacing(0);
        m_tabs = new QTabBar;
        m_tabs->setDrawBase(false);
        m_tabs->setElideMode(Qt::ElideNone);
        m_tabs->setUsesScrollButtons(false);
        m_tabs->setExpanding(false);
        for (const char *t : {"Overview", "CPU", "Memory", "Disk", "Networking"}) {
            m_tabs->addTab(tr7(t));
        }
        auto *tabRow = new QHBoxLayout;
        tabRow->setContentsMargins(4, 0, 0, 0);
        tabRow->addWidget(m_tabs);
        tabRow->addStretch(1);
        cv->addLayout(tabRow);
        auto *page = new QWidget;
        page->setObjectName(QStringLiteral("resmonpage"));
        page->setAttribute(Qt::WA_StyledBackground, true);
        page->setStyleSheet(QStringLiteral("#resmonpage { background: white; border-top: 1px solid #898c95; }"));
        auto *pv = new QHBoxLayout(page);
        pv->setContentsMargins(2, 4, 2, 2);
        m_split = new QSplitter(Qt::Horizontal);
        m_split->setChildrenCollapsible(false);
        m_left = new QStackedWidget;
        m_graphs = new GraphPane;
        m_graphs->setMinimumWidth(180);
        m_split->addWidget(m_left);
        m_split->addWidget(m_graphs);
        m_split->setStretchFactor(0, 1);
        m_split->setSizes({700, 270});
        m_showGraphs = new RoundButton(RoundButton::Left);
        m_showGraphs->setToolTip(tr7("Show Graphs"));
        m_showGraphs->hide();
        pv->addWidget(m_split, 1);
        pv->addWidget(m_showGraphs, 0, Qt::AlignTop);
        cv->addWidget(page, 1);
        setCentralWidget(central);
        connect(m_graphs->hide, &QAbstractButton::clicked, this, [this] {
            m_graphs->setVisible(false);
            m_showGraphs->show();
        });
        connect(m_showGraphs, &QAbstractButton::clicked, this, [this] {
            m_graphs->setVisible(true);
            m_showGraphs->hide();
        });

        buildOverview();
        buildCpu();
        buildMemory();
        buildDisk();
        buildNetwork();
        connect(m_tabs, &QTabBar::currentChanged, this, [this](int i) {
            m_left->setCurrentIndex(i);
            m_graphs->setPage(i);
            refreshSlow();
        });

        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, [this] {
            sample();
        });
        m_slow = new QTimer(this);
        connect(m_slow, &QTimer::timeout, this, [this] {
            refreshSlow();
        });
        sample();
        refreshSlow();
        m_timer->start(1000);
        m_slow->start(4000);
    }

    void setTab(int i)
    {
        m_tabs->setCurrentIndex(i);
    }

private:
    static QWidget *column(const QList<QWidget *> &sections)
    {
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *w = new QWidget;
        w->setObjectName(QStringLiteral("resmoncol"));
        w->setAttribute(Qt::WA_StyledBackground, true);
        w->setStyleSheet(QStringLiteral("#resmoncol { background: white; }"));
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(0, 0, 4, 0);
        v->setSpacing(2);
        for (QWidget *s : sections) {
            v->addWidget(s);
        }
        v->addStretch(1);
        scroll->setWidget(w);
        return scroll;
    }

    Table *processTable(const QStringList &cols, const QList<int> &widths, int stretch)
    {
        auto *t = new Table(cols, widths, true, stretch);
        t->view()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(t->view(), &QWidget::customContextMenuRequested, this, [this, t](QPoint pos) {
            const QModelIndex idx = t->view()->indexAt(pos);
            if (!idx.isValid()) {
                return;
            }
            processMenu(idx.siblingAtColumn(0).data(Qt::UserRole).toInt(), t->view()->viewport()->mapToGlobal(pos));
        });
        t->checkedChanged = [this, t] {
            syncChecks(t);
        };
        m_procTables << t;
        return t;
    }

    /** All process lists share the checked processes, like in Windows */
    void syncChecks(Table *from)
    {
        m_checked = from->checked();
        for (Table *t : std::as_const(m_procTables)) {
            if (t != from) {
                t->setChecked(m_checked);
            }
        }
        refreshSlow(); // the other filtered lists follow on the next tick
    }

    QString filterText() const
    {
        if (m_checked.isEmpty()) {
            return {};
        }
        QStringList names;
        for (const QString &k : m_checked) {
            names << m_names.value(k.toInt(), k);
        }
        names.sort();
        names.removeDuplicates();
        return tr7("Filtered by: ") + names.join(QStringLiteral(", "));
    }

    /* ---- tabs ---- */

    static QString L(const char *t)
    {
        return tr7(t);
    }

    void buildOverview()
    {
        const QStringList cpuCols{L("Image"), L("PID"), L("Description"), L("Status"), L("Threads"), L("CPU"), L("Average CPU")};
        const QList<int> cpuW{190, 80, 300, 110, 70, 50, 110};
        m_ovCpu = processTable(cpuCols, cpuW, 2);
        auto *sCpu = new Section(L("CPU"), m_ovCpu, 230);
        m_iOvCpu = sCpu->addIndicator(QColor(0x00, 0xc0, 0x00));
        m_iOvFreq = sCpu->addIndicator(QColor(0x70, 0xa8, 0xff));
        m_ovDisk = new Table({L("Image"), L("PID"), L(""), L("Read (B/sec)"), L("Write (B/sec)"), L("Total (B/sec)"), L("I/O Priority")},
                             {190, 80, 300, 130, 130, 130, 160}, false, 2);
        auto *sDisk = new Section(L("Disk"), m_ovDisk, 230);
        m_iOvDisk = sDisk->addIndicator(QColor(0x00, 0xc0, 0x00));
        m_iOvDiskTime = sDisk->addIndicator(QColor(0x00, 0x00, 0xe0));
        sDisk->setExpanded(false);
        m_ovNet = new Table({L("Image"), L("PID"), L("Address"), L("Send (B/sec)"), L("Receive (B/sec)"), L("Total (B/sec)")},
                            {190, 80, 300, 140, 140, 130}, false, 2);
        auto *sNet = new Section(L("Networking"), m_ovNet, 230);
        m_iOvNet = sNet->addIndicator(QColor(0x00, 0x80, 0x00));
        m_iOvNetUse = sNet->addIndicator(QColor(0x00, 0x00, 0xb0));
        sNet->setExpanded(false);
        const QStringList memCols{L("Image"), L("PID"), L("Hard Faults/sec"), L("Commit (KB)"), L("Working Set (KB)"), L("Shareable (KB)"), L("Private (KB)")};
        const QList<int> memW{190, 80, 300, 120, 130, 110, 110};
        m_ovMem = processTable(memCols, memW, 2);
        auto *sMem = new Section(L("Memory"), m_ovMem, 230);
        m_iOvFaults = sMem->addIndicator(QColor(0x00, 0x80, 0x00));
        m_iOvMemUse = sMem->addIndicator(QColor(0x70, 0xa8, 0xff));
        sMem->setExpanded(false);
        m_left->addWidget(column({sCpu, sDisk, sNet, sMem}));
        m_gOvCpu = new ResGraph(L("CPU"));
        m_gOvDisk = new ResGraph(L("Disk"));
        m_gOvNet = new ResGraph(L("Networking"));
        m_gOvMem = new ResGraph(L("Memory"));
        m_graphs->addPage({m_gOvCpu, m_gOvDisk, m_gOvNet, m_gOvMem});
        m_ovCpu->sortBy(6, Qt::DescendingOrder);
        m_ovDisk->sortBy(5, Qt::DescendingOrder);
        m_ovNet->sortBy(5, Qt::DescendingOrder);
        m_ovMem->sortBy(6, Qt::DescendingOrder);
    }

    void buildCpu()
    {
        m_cpuProcs = processTable({L("Image"), L("PID"), L("Description"), L("Status"), L("Threads"), L("CPU"), L("Average CPU")}, {190, 80, 300, 110, 70, 50, 110}, 2);
        auto *sProcs = new Section(L("Processes"), m_cpuProcs, 230);
        m_iCpuProcs = sProcs->addIndicator(QColor(0x00, 0xc0, 0x00));
        m_iCpuFreq = sProcs->addIndicator(QColor(0x70, 0xa8, 0xff));
        m_services = new Table({L("Name"), L("PID"), L("Description"), L("Status"), L("Group"), L("CPU"), L("Average CPU")}, {190, 80, 300, 110, 140, 50, 110}, false, 2);
        m_services->view()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_services->view(), &QWidget::customContextMenuRequested, this, [this](QPoint pos) {
            const QModelIndex idx = m_services->view()->indexAt(pos);
            if (idx.isValid()) {
                serviceMenu(idx.siblingAtColumn(0).data(Qt::UserRole).toString(), m_services->view()->viewport()->mapToGlobal(pos));
            }
        });
        m_sServices = new Section(L("Services"), m_services, 230);
        m_iServices = m_sServices->addIndicator(QColor(0x00, 0x80, 0x00));
        m_handles = new Table({L("Image"), L("PID"), L("Type"), L("Handle Name")}, {190, 80, 110, 400}, false, 3);
        m_handles->setPlaceholder(L("Select a process or search handles to view results."));
        m_sHandles = new Section(L("Associated Handles"), m_handles, 230);
        m_sHandles->setExpanded(false);
        m_search = new QLineEdit;
        m_search->setPlaceholderText(L("Search Handles"));
        m_search->setClearButtonEnabled(true);
        connect(m_search, &QLineEdit::returnPressed, this, [this] {
            refreshHandles();
        });
        m_modules = new Table({L("Image"), L("PID"), L("Module Name"), L("Version"), L("Full Path")}, {190, 80, 200, 90, 400}, false, 4);
        m_modules->setPlaceholder(L("Select a process to view results."));
        m_sModules = new Section(L("Associated Modules"), m_modules, 230);
        m_sModules->setExpanded(false);
        m_left->addWidget(column({sProcs, m_sServices, m_sHandles, m_sModules}));
        m_gCpuTotal = new ResGraph(L("CPU - Total"));
        m_gCpuServices = new ResGraph(L("Service CPU Usage"));
        QList<ResGraph *> graphs{m_gCpuTotal, m_gCpuServices};
        for (int i = 0; i < m_ncpu; ++i) {
            m_gCpuCores << new ResGraph(L("CPU %1").arg(i));
            graphs << m_gCpuCores.last();
        }
        m_graphs->addPage(graphs);
        m_cpuProcs->sortBy(6, Qt::DescendingOrder);
        m_services->sortBy(6, Qt::DescendingOrder);
    }

    void buildMemory()
    {
        m_memProcs = processTable({L("Image"), L("PID"), L("Hard Faults/sec"), L("Commit (KB)"), L("Working Set (KB)"), L("Shareable (KB)"), L("Private (KB)")},
                                  {190, 80, 300, 120, 130, 110, 110}, 2);
        auto *sProcs = new Section(L("Processes"), m_memProcs, 230);
        m_iMemUsed = sProcs->addIndicator(QColor(0x00, 0xc0, 0x00));
        m_memBar = new MemoryBar;
        auto *sPhys = new Section(L("Physical Memory"), m_memBar, 240);
        m_iMemInUse = sPhys->addIndicator(QColor(0x00, 0xc0, 0x00));
        m_iMemAvail = sPhys->addIndicator(QColor(0x00, 0x00, 0xe0));
        m_left->addWidget(column({sProcs, sPhys}));
        m_gMemUsed = new ResGraph(L("Used Physical Memory"));
        m_gMemCommit = new ResGraph(L("Commit Charge"));
        m_gMemFaults = new ResGraph(tr7("Hard Faults/sec", "graph"));
        m_graphs->addPage({m_gMemUsed, m_gMemCommit, m_gMemFaults});
        m_memProcs->sortBy(6, Qt::DescendingOrder);
    }

    void buildDisk()
    {
        m_diskProcs = processTable({L("Image"), L("PID"), L(""), L("Read (B/sec)"), L("Write (B/sec)"), L("Total (B/sec)")}, {190, 80, 300, 130, 130, 130}, 2);
        auto *sProcs = new Section(L("Processes with Disk Activity"), m_diskProcs, 230);
        m_diskActivity = new Table({L("Image"), L("PID"), L(""), L("Read (B/sec)"), L("Write (B/sec)"), L("Total (B/sec)"), L("I/O Priority")},
                                   {190, 80, 300, 130, 130, 130, 160}, false, 2);
        m_sDiskActivity = new Section(L("Disk Activity"), m_diskActivity, 230);
        m_iDiskIo = m_sDiskActivity->addIndicator(QColor(0x00, 0xc0, 0x00));
        m_iDiskTime = m_sDiskActivity->addIndicator(QColor(0x00, 0x00, 0xe0));
        m_storage = new Table({L("Logical Disk"), L("Physical Disk"), L("Active Time (%)"), L("Available Space (MB)"), L("Total Space (MB)"), L("Disk Queue Length")},
                              {190, 300, 130, 120, 120, 150}, false, 1);
        auto *sStorage = new Section(L("Storage"), m_storage, 180);
        m_left->addWidget(column({sProcs, m_sDiskActivity, sStorage}));
        m_gDisk = new ResGraph(L("Disk"));
        QList<ResGraph *> graphs{m_gDisk};
        int i = 0;
        for (const DiskStat &d : readDisks()) {
            const QString mount = mountOf(d.name);
            auto *g = new ResGraph(L("Disk %1 (%2) Queue Length").arg(i++).arg(mount.isEmpty() ? d.name : mount));
            m_gQueues.insert(d.name, g);
            graphs << g;
        }
        m_graphs->addPage(graphs);
        m_diskProcs->sortBy(5, Qt::DescendingOrder);
        m_diskActivity->sortBy(5, Qt::DescendingOrder);
    }

    void buildNetwork()
    {
        m_netProcs = processTable({L("Image"), L("PID"), L(""), L("Send (B/sec)"), L("Receive (B/sec)"), L("Total (B/sec)")}, {190, 80, 300, 140, 140, 130}, 2);
        auto *sProcs = new Section(L("Processes with Network Activity"), m_netProcs, 230);
        m_netActivity = new Table({L("Image"), L("PID"), L("Address"), L("Send (B/sec)"), L("Receive (B/sec)"), L("Total (B/sec)")},
                                  {190, 80, 300, 140, 140, 130}, false, 2);
        m_sNetActivity = new Section(L("Network Activity"), m_netActivity, 230);
        m_iNetIo = m_sNetActivity->addIndicator(QColor(0x00, 0xc0, 0x00));
        m_iNetUse = m_sNetActivity->addIndicator(QColor(0x00, 0x00, 0xe0));
        m_tcp = new Table({L("Image"), L("PID"), L("Local Address"), L("Local Port"), L("Remote Address"), L("Remote Port"), L("Packet Loss (%)"), L("Latency (ms)")},
                          {190, 80, 200, 110, 200, 120, 130, 110}, false, 4);
        m_sTcp = new Section(L("TCP Connections"), m_tcp, 230);
        m_sTcp->setExpanded(false);
        m_listen = new Table({L("Image"), L("PID"), L("Address"), L("Port"), L("Protocol"), L("Firewall Status")}, {190, 80, 300, 80, 90, 260}, false, 2);
        m_sListen = new Section(L("Listening Ports"), m_listen, 230);
        m_sListen->setExpanded(false);
        m_left->addWidget(column({sProcs, m_sNetActivity, m_sTcp, m_sListen}));
        m_gNet = new ResGraph(L("Networking"));
        m_gTcp = new ResGraph(L("TCP Connections"));
        QList<ResGraph *> graphs{m_gNet, m_gTcp};
        for (const NetDev &d : readNet()) {
            auto *g = new ResGraph(netTitle(d.name));
            m_gIfaces.insert(d.name, g);
            graphs << g;
        }
        m_graphs->addPage(graphs);
        m_netProcs->sortBy(5, Qt::DescendingOrder);
        m_netActivity->sortBy(5, Qt::DescendingOrder);
    }

    static QString netTitle(const QString &iface)
    {
        if (QFileInfo::exists(QStringLiteral("/sys/class/net/%1/wireless").arg(iface))) {
            return tr7("Wireless Network Connection (%1)").arg(iface);
        }
        return tr7("Local Area Connection (%1)").arg(iface);
    }

    static QString mountOf(const QString &disk)
    {
        QProcess p;
        p.start(QStringLiteral("lsblk"), {QStringLiteral("-nrpo"), QStringLiteral("MOUNTPOINT"), QStringLiteral("/dev/") + disk});
        p.waitForFinished(2000);
        QStringList mps = QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (mps.contains(QStringLiteral("/"))) {
            return QStringLiteral("/");
        }
        return mps.value(0).replace(QStringLiteral("\\x20"), QStringLiteral(" "));
    }

    /* ---- sampling ---- */

    void setMonitoring(bool on)
    {
        m_stop->setEnabled(on);
        m_start->setEnabled(!on);
        if (on) {
            m_timer->start(1000);
            m_slow->start(4000);
        } else {
            m_timer->stop();
            m_slow->stop();
        }
    }

    void sample()
    {
        const bool advance = true;
        const qint64 ms = m_clock.isValid() ? qMax<qint64>(1, m_clock.restart()) : 1000;
        if (!m_clock.isValid()) {
            m_clock.start();
        }
        const double secs = advance ? ms / 1000.0 : 1.0;
        // CPU totals
        const QList<CpuTimes> cpu = readCpuTimes();
        double total = 0;
        QList<double> cores;
        quint64 totalDelta = 1;
        if (m_prevCpu.size() == cpu.size()) {
            for (int i = 0; i < cpu.size(); ++i) {
                const quint64 dt = cpu.at(i).total - m_prevCpu.at(i).total;
                const double use = dt ? double(cpu.at(i).busy - m_prevCpu.at(i).busy) / dt : 0;
                if (i == 0) {
                    total = use;
                    totalDelta = qMax<quint64>(1, dt);
                } else {
                    cores << use;
                }
            }
        }
        if (advance) {
            m_prevCpu = cpu;
        }
        // processes
        QList<Proc> procs = readProcs();
        QHash<int, Proc> prev;
        for (const Proc &p : std::as_const(m_lastProcs)) {
            prev.insert(p.pid, p);
        }
        double checkedCpu = 0;
        double faultsTotal = 0;
        for (Proc &p : procs) {
            const auto it = prev.constFind(p.pid);
            if (it != prev.constEnd()) {
                p.cpu = 100.0 * double(p.ticks - it->ticks) / totalDelta;
                p.faults = (p.majflt - it->majflt) / secs;
                if (p.ioKnown && it->ioKnown) {
                    p.readRate = (p.readBytes - it->readBytes) / secs;
                    p.writeRate = (p.writeBytes - it->writeBytes) / secs;
                }
            }
            auto &hist = m_cpuHistory[p.pid];
            hist.append(p.cpu);
            while (hist.size() > 60) {
                hist.removeFirst();
            }
            double sum = 0;
            for (double v : hist) {
                sum += v;
            }
            p.cpuAvg = sum / hist.size();
            faultsTotal += p.faults;
            m_names.insert(p.pid, p.image);
            if (m_checked.contains(p.key())) {
                checkedCpu += p.cpu;
            }
        }
        // network sockets -> per-process rates
        const QList<Socket> sockets = readTcp();
        QHash<QString, Socket> prevSock;
        for (const Socket &s : std::as_const(m_lastSockets)) {
            prevSock.insert(s.key, s);
        }
        QList<Socket> socks = sockets;
        QHash<int, QPair<double, double>> procNet;
        for (Socket &s : socks) {
            const auto it = prevSock.constFind(s.key);
            if (it != prevSock.constEnd()) {
                s.sendRate = qMax(0.0, double(s.sent - it->sent) / secs);
                s.recvRate = qMax(0.0, double(s.received - it->received) / secs);
            }
            procNet[s.pid].first += s.sendRate;
            procNet[s.pid].second += s.recvRate;
        }
        if (advance) {
            m_lastSockets = sockets;
        }
        for (Proc &p : procs) {
            p.sendRate = procNet.value(p.pid).first;
            p.recvRate = procNet.value(p.pid).second;
        }
        // disks
        const QList<DiskStat> disks = readDisks();
        double diskBytes = 0, busiest = 0;
        QHash<QString, double> queue;
        for (const DiskStat &d : disks) {
            for (const DiskStat &o : std::as_const(m_prevDisks)) {
                if (o.name == d.name) {
                    diskBytes += ((d.readSectors - o.readSectors) + (d.writeSectors - o.writeSectors)) * 512.0 / secs;
                    busiest = qMax(busiest, qMin(1.0, (d.ioTicks - o.ioTicks) / double(ms)));
                    queue.insert(d.name, (d.weightedTicks - o.weightedTicks) / double(ms));
                    m_queueLen.insert(d.name, (d.weightedTicks - o.weightedTicks) / double(ms));
                    m_activeTime.insert(d.name, qMin(100.0, (d.ioTicks - o.ioTicks) * 100.0 / ms));
                }
            }
        }
        if (advance) {
            m_prevDisks = disks;
        }
        // network interfaces
        const QList<NetDev> nets = readNet();
        double netBits = 0, netUse = 0;
        QHash<QString, double> ifaceUse;
        for (const NetDev &d : nets) {
            for (const NetDev &o : std::as_const(m_prevNet)) {
                if (o.name == d.name) {
                    const double bits = ((d.rx - o.rx) + (d.tx - o.tx)) * 8.0 / secs;
                    netBits += bits;
                    const double use = bits / (d.speedMbit * 1e6);
                    ifaceUse.insert(d.name, use);
                    netUse = qMax(netUse, use);
                }
            }
        }
        if (advance) {
            m_prevNet = nets;
        }
        // memory
        const MemInfo mem = readMem();
        const quint64 majfault = keyValue(readFile(QStringLiteral("/proc/vmstat")), QStringLiteral("pgmajfault"), QLatin1Char(' ')).toULongLong();
        const double faults = m_prevMajfault ? (majfault - m_prevMajfault) / secs : faultsTotal;
        if (advance) {
            m_prevMajfault = majfault;
        }
        const double usedPhys = mem.total ? double(mem.total - mem.available) / mem.total : 0;
        // CPU frequency (Maximum Frequency)
        double freq = 0;
        int nf = 0;
        for (int i = 0; i < m_ncpu; ++i) {
            const double cur = readFile(QStringLiteral("/sys/devices/system/cpu/cpu%1/cpufreq/scaling_cur_freq").arg(i)).trimmed().toDouble();
            const double max = readFile(QStringLiteral("/sys/devices/system/cpu/cpu%1/cpufreq/cpuinfo_max_freq").arg(i)).trimmed().toDouble();
            if (max > 0) {
                freq += cur / max;
                ++nf;
            }
        }
        freq = nf ? freq / nf : 1;

        // header indicators
        const QString cpuText = tr7("%1% CPU Usage").arg(qRound(total * 100));
        const QString freqText = tr7("%1% Maximum Frequency").arg(qRound(freq * 100));
        m_iOvCpu->setText(cpuText);
        m_iCpuProcs->setText(cpuText);
        m_iOvFreq->setText(freqText);
        m_iCpuFreq->setText(freqText);
        const QString diskText = diskBytes >= 1024 * 1024 ? tr7("%1 KB/sec Disk I/O").arg(group(qRound64(diskBytes / 1024)))
                                                          : tr7("%1 B/sec Disk I/O").arg(group(qRound64(diskBytes)));
        const QString timeText = tr7("%1% Highest Active Time").arg(qRound(busiest * 100));
        m_iOvDisk->setText(diskText);
        m_iDiskIo->setText(diskText);
        m_iOvDiskTime->setText(timeText);
        m_iDiskTime->setText(timeText);
        const QString netText = tr7("%1 Kbps Network I/O").arg(group(qRound64(netBits / 1000)));
        const QString useText = tr7("%1% Network Utilization").arg(qRound(netUse * 100));
        m_iOvNet->setText(netText);
        m_iNetIo->setText(netText);
        m_iOvNetUse->setText(useText);
        m_iNetUse->setText(useText);
        m_iOvFaults->setText(tr7("%1 Hard Faults/sec").arg(qRound(faults)));
        m_iOvMemUse->setText(tr7("%1% Used Physical Memory").arg(qRound(usedPhys * 100)));
        m_iMemUsed->setText(tr7("%1% Used Physical Memory").arg(qRound(usedPhys * 100)));
        m_iMemInUse->setText(tr7("In Use: %1 MB").arg(group(mem.inUse / 1024)));
        m_iMemAvail->setText(tr7("Available: %1 MB").arg(group(mem.available / 1024)));
        m_memBar->setInfo(mem);

        // graphs
        if (advance) {
            const double orange = m_checked.isEmpty() ? -1 : checkedCpu / 100.0;
            m_gOvCpu->add(total, freq, orange);
            m_gCpuTotal->add(total, freq, orange);
            for (int i = 0; i < m_gCpuCores.size() && i < cores.size(); ++i) {
                m_gCpuCores.at(i)->add(cores.at(i));
            }
            m_gCpuServices->add(m_servicesCpu / 100.0);
            const double diskScale = autoScale(diskBytes, {100e3, 1e6, 10e6, 100e6, 1e9});
            const QString diskLabel = diskScale >= 1e6 ? tr7("%1 MB/sec").arg(diskScale / 1e6) : tr7("%1 KB/sec").arg(diskScale / 1e3);
            for (ResGraph *g : {m_gOvDisk, m_gDisk}) {
                g->setScale(diskLabel, QStringLiteral("0"));
                g->add(diskBytes / diskScale, busiest);
            }
            for (auto it = m_gQueues.begin(); it != m_gQueues.end(); ++it) {
                const double q = queue.value(it.key());
                const double scale = autoScale(q, {0.01, 0.1, 1, 10, 100});
                it.value()->setScale(num(scale, scale < 1 ? 2 : 0), QStringLiteral("0"));
                it.value()->add(q / scale);
            }
            const double netScale = autoScale(netBits, {100e3, 1e6, 10e6, 100e6, 1e9});
            const QString netLabel = netScale >= 1e6 ? tr7("%1 Mbps").arg(netScale / 1e6) : tr7("%1 Kbps").arg(netScale / 1e3);
            for (ResGraph *g : {m_gOvNet, m_gNet}) {
                g->setScale(netLabel, QStringLiteral("0"));
                g->add(netBits / netScale, netUse);
            }
            const double tcpScale = autoScale(sockets.size(), {10, 50, 100, 500, 1000, 5000});
            m_gTcp->setScale(QString::number(int(tcpScale)), QStringLiteral("0"));
            m_gTcp->add(sockets.size() / tcpScale);
            for (auto it = m_gIfaces.begin(); it != m_gIfaces.end(); ++it) {
                it.value()->add(ifaceUse.value(it.key()));
            }
            const double faultScale = autoScale(faults, {100, 1000, 10000, 100000});
            for (ResGraph *g : {m_gOvMem, m_gMemFaults}) {
                g->setScale(tr7("%1 Hard Faults/sec").arg(group(qint64(faultScale))), QStringLiteral("0"));
                if (g == m_gOvMem) {
                    g->add(faults / faultScale, usedPhys);
                } else {
                    g->setScale(QString::number(qint64(faultScale)), QStringLiteral("0"));
                    g->add(faults / faultScale);
                }
            }
            m_gMemUsed->add(usedPhys);
            m_gMemCommit->add(mem.commitLimit ? double(mem.commit) / mem.commitLimit : 0);
        }

        // tables. Disk and network columns are averages of the last minute, like in Windows
        QList<Table::Row> cpuRows, memRows, diskRows, netRows, diskActRows;
        auto cell = [](const QString &t) {
            return Table::Cell{t, 0, false};
        };
        auto ncell = [](double v, int prec = 0) {
            return Table::Cell{prec ? num(v, prec) : group(qRound64(v)), v, true};
        };
        auto minute = [](QList<QPair<double, double>> &h, double a, double b) {
            h.append({a, b});
            while (h.size() > 60) {
                h.removeFirst();
            }
            QPair<double, double> sum{0, 0};
            for (const auto &v : h) {
                sum.first += v.first;
                sum.second += v.second;
            }
            return QPair<double, double>{sum.first / 60.0, sum.second / 60.0};
        };
        QSet<int> alive;
        for (const Proc &p : std::as_const(procs)) {
            alive.insert(p.pid);
            cpuRows.append({p.key(), {cell(p.image), ncell(p.pid), cell(p.description), cell(p.status()), ncell(p.threads), ncell(p.cpu), ncell(p.cpuAvg, 2)}});
            memRows.append({p.key(), {cell(p.image), ncell(p.pid), ncell(p.faults), ncell(p.anonKb + p.swapKb), ncell(p.rssKb), ncell(p.fileKb + p.shmemKb), ncell(p.anonKb)}});
            const auto disk = minute(m_diskHist[p.pid], p.readRate, p.writeRate);
            if (disk.first + disk.second > 0) {
                diskRows.append({p.key(), {cell(p.image), ncell(p.pid), cell(QString()), ncell(disk.first), ncell(disk.second), ncell(disk.first + disk.second), cell(ioPriority(p.pid))}});
            }
            const auto net = minute(m_netHist[p.pid], p.sendRate, p.recvRate);
            if (net.first + net.second > 0) {
                netRows.append({p.key(), {cell(p.image), ncell(p.pid), cell(QString()), ncell(net.first), ncell(net.second), ncell(net.first + net.second)}});
            }
        }
        for (auto it = m_diskHist.begin(); it != m_diskHist.end();) {
            it = alive.contains(it.key()) ? std::next(it) : m_diskHist.erase(it);
        }
        for (auto it = m_netHist.begin(); it != m_netHist.end();) {
            it = alive.contains(it.key()) ? std::next(it) : m_netHist.erase(it);
        }
        m_ovCpu->update(cpuRows);
        m_cpuProcs->update(cpuRows);
        m_ovMem->update(memRows);
        m_memProcs->update(memRows);
        m_ovDisk->update(diskRows);
        QList<Table::Row> diskProcRows;
        for (const Table::Row &r : std::as_const(diskRows)) {
            diskProcRows.append({r.key, r.cells.mid(0, 6)});
            if (m_checked.isEmpty() || m_checked.contains(r.key)) {
                diskActRows.append(r);
            }
        }
        m_diskProcs->update(diskProcRows);
        m_diskActivity->update(diskActRows);
        m_sDiskActivity->setFilter(filterText());
        m_netProcs->update(netRows);
        // per address activity and the overview network list (minute averages too)
        QHash<QString, QPair<double, double>> nowByAddress;
        QHash<QString, QPair<QString, int>> who;
        for (const Socket &s : std::as_const(socks)) {
            const QString k = QString::number(s.pid) + QLatin1Char('|') + s.peer;
            nowByAddress[k].first += s.sendRate;
            nowByAddress[k].second += s.recvRate;
            who.insert(k, {s.image, s.pid});
        }
        QHash<QString, Table::Row> byAddress;
        for (auto it = m_addrHist.begin(); it != m_addrHist.end();) {
            if (!nowByAddress.contains(it.key())) {
                nowByAddress.insert(it.key(), {0, 0});
            }
            ++it;
        }
        for (auto it = nowByAddress.cbegin(); it != nowByAddress.cend(); ++it) {
            const auto avg = minute(m_addrHist[it.key()], it.value().first, it.value().second);
            if (avg.first + avg.second <= 0) {
                m_addrHist.remove(it.key());
                continue;
            }
            if (who.contains(it.key())) {
                m_addrWho.insert(it.key(), who.value(it.key()));
            }
            const auto w = m_addrWho.value(it.key());
            byAddress.insert(it.key(), {it.key(), {cell(w.first), ncell(w.second), cell(it.key().section(QLatin1Char('|'), 1)), ncell(avg.first), ncell(avg.second),
                                                   ncell(avg.first + avg.second)}});
        }
        QList<Table::Row> addrRows, addrFiltered;
        for (const Table::Row &r : std::as_const(byAddress)) {
            addrRows.append(r);
            if (m_checked.isEmpty() || m_checked.contains(r.key.section(QLatin1Char('|'), 0, 0))) {
                addrFiltered.append(r);
            }
        }
        m_ovNet->update(addrRows);
        m_netActivity->update(addrFiltered);
        m_sNetActivity->setFilter(filterText());
        QList<Table::Row> tcpRows;
        for (const Socket &s : std::as_const(socks)) {
            if (!m_checked.isEmpty() && !m_checked.contains(QString::number(s.pid))) {
                continue;
            }
            auto loop = [](const QString &a) {
                return a.startsWith(QLatin1String("127.")) ? tr7("IPv4 loopback") : a == QLatin1String("::1") ? tr7("IPv6 loopback") : a;
            };
            tcpRows.append({s.key, {cell(s.image.isEmpty() ? tr7("System") : s.image), ncell(s.pid), cell(loop(s.local)), ncell(s.localPort.toDouble()), cell(loop(s.peer)),
                                    ncell(s.peerPort.toDouble()), ncell(s.loss), s.rtt < 0 ? cell(QString()) : ncell(s.rtt)}});
        }
        m_tcp->update(tcpRows);
        m_sTcp->setFilter(filterText());
        if (advance) {
            m_lastProcs = procs;
        }
    }

    /** Things that change slowly or cost more: services, handles, modules, storage, listening ports */
    void refreshSlow()
    {
        const int tab = m_tabs->currentIndex();
        if (tab == 1) {
            refreshServices();
            refreshHandles();
            refreshModules();
        }
        if (tab == 3) {
            refreshStorage();
        }
        if (tab == 4) {
            refreshListeners();
        }
        if (tab != 1) {
            refreshServices(); // feeds the "Service CPU Usage" graph
        }
    }

    void refreshServices()
    {
        const qint64 ms = m_serviceClock.isValid() ? qMax<qint64>(1, m_serviceClock.restart()) : 0;
        if (!m_serviceClock.isValid()) {
            m_serviceClock.start();
        }
        QList<Service> list = readServices();
        QList<Table::Row> rows;
        double sum = 0;
        for (Service &s : list) {
            const quint64 prev = m_prevServices.value(s.name);
            if (prev && ms) {
                s.cpu = (s.usageUsec - prev) / 10.0 / ms / m_ncpu;
            }
            auto &hist = m_serviceHistory[s.name];
            hist.append(s.cpu);
            while (hist.size() > 15) {
                hist.removeFirst();
            }
            double avg = 0;
            for (double v : hist) {
                avg += v;
            }
            s.cpuAvg = avg / hist.size();
            sum += s.cpu;
            rows.append({s.name, {{s.name, 0, false}, {s.pid ? QString::number(s.pid) : QString(), double(s.pid), true}, {s.description, 0, false},
                                  {s.running ? tr7("Running", "service") : tr7("Stopped"), 0, false}, {s.group, 0, false},
                                  {num(s.cpu), s.cpu, true}, {num(s.cpuAvg, 2), s.cpuAvg, true}}});
        }
        m_prevServices.clear();
        for (const Service &s : std::as_const(list)) {
            m_prevServices.insert(s.name, s.usageUsec);
        }
        m_servicesCpu = sum;
        m_services->update(rows);
        m_iServices->setText(tr7("%1% CPU Usage").arg(qRound(sum)));
    }

    static QString fdType(const QString &target)
    {
        if (target.startsWith(QLatin1String("socket:"))) {
            return tr7("Socket");
        }
        if (target.startsWith(QLatin1String("pipe:"))) {
            return tr7("Pipe");
        }
        if (target.startsWith(QLatin1String("anon_inode:"))) {
            return tr7("Event");
        }
        if (target.startsWith(QLatin1String("/dev/"))) {
            return tr7("Device");
        }
        if (QFileInfo(target).isDir()) {
            return tr7("Folder");
        }
        return tr7("File");
    }

    void refreshHandles()
    {
        const QString needle = m_search->text().trimmed();
        QList<int> pids;
        if (!needle.isEmpty()) {
            for (const QString &p : QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                if (p.toInt()) {
                    pids << p.toInt();
                }
            }
        } else {
            for (const QString &k : std::as_const(m_checked)) {
                pids << k.toInt();
            }
        }
        QList<Table::Row> rows;
        for (int pid : pids) {
            const QDir fd(QStringLiteral("/proc/%1/fd").arg(pid));
            for (const QString &n : fd.entryList(QDir::System | QDir::NoDotAndDotDot)) {
                QString target;
                char buf[4096];
                const ssize_t len = readlink(QFile::encodeName(fd.filePath(n)).constData(), buf, sizeof buf - 1);
                if (len > 0) {
                    target = QString::fromLocal8Bit(buf, int(len));
                }
                if (!needle.isEmpty() && !target.contains(needle, Qt::CaseInsensitive)) {
                    continue;
                }
                rows.append({QStringLiteral("%1/%2").arg(pid).arg(n), {{m_names.value(pid), 0, false}, {QString::number(pid), double(pid), true}, {fdType(target), 0, false}, {target, 0, false}}});
                if (rows.size() > 3000) {
                    break;
                }
            }
        }
        m_handles->update(rows);
        m_sHandles->setFilter(needle.isEmpty() ? filterText() : tr7("Search results for \"%1\"").arg(needle));
    }

    void refreshModules()
    {
        QList<Table::Row> rows;
        static const QRegularExpression ver(QStringLiteral("\\.so\\.([0-9][0-9.]*)$"));
        for (const QString &k : std::as_const(m_checked)) {
            const int pid = k.toInt();
            QSet<QString> seen;
            for (const QString &line : readFile(QStringLiteral("/proc/%1/maps").arg(pid)).split(QLatin1Char('\n'))) {
                const int slash = line.indexOf(QLatin1Char('/'));
                if (slash < 0) {
                    continue;
                }
                const QString path = line.mid(slash).trimmed();
                if (seen.contains(path) || path.startsWith(QLatin1String("/dev/")) || path.startsWith(QLatin1String("/memfd")) || path.contains(QLatin1String(" (deleted)"))) {
                    continue;
                }
                const QString name = QFileInfo(path).fileName();
                if (!name.contains(QLatin1String(".so")) && QFileInfo(path).fileName() != m_names.value(pid)) {
                    continue;
                }
                seen.insert(path);
                rows.append({k + QLatin1Char('|') + path, {{m_names.value(pid), 0, false}, {QString::number(pid), double(pid), true}, {name, 0, false},
                                                            {ver.match(name).captured(1), 0, false}, {path, 0, false}}});
            }
        }
        m_modules->update(rows);
        m_sModules->setFilter(filterText());
    }

    /** One row per physical disk with all its mount points, like "C: D:" in Windows */
    void refreshStorage()
    {
        QProcess p;
        p.start(QStringLiteral("df"), {QStringLiteral("-B1M"), QStringLiteral("--output=source,target,avail,size"), QStringLiteral("-x"), QStringLiteral("tmpfs"), QStringLiteral("-x"),
                                       QStringLiteral("devtmpfs"), QStringLiteral("-x"), QStringLiteral("squashfs"), QStringLiteral("-x"), QStringLiteral("efivarfs"), QStringLiteral("-x"),
                                       QStringLiteral("overlay")});
        p.waitForFinished(3000);
        const QStringList lines = QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QList<DiskStat> disks = readDisks();
        QList<Table::Row> rows;
        for (int k = 0; k < disks.size(); ++k) {
            const QString disk = disks.at(k).name;
            QStringList mounts;
            qint64 avail = 0, size = 0;
            for (int i = 1; i < lines.size(); ++i) {
                const QStringList f = lines.at(i).split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (f.size() < 4 || !f.at(0).startsWith(QStringLiteral("/dev/") + disk)) {
                    continue;
                }
                mounts << f.at(1);
                avail += f.at(2).toLongLong();
                size += f.at(3).toLongLong();
            }
            if (mounts.isEmpty()) {
                continue;
            }
            const double active = m_activeTime.value(disk);
            const double queue = m_queueLen.value(disk);
            rows.append({disk, {{mounts.join(QLatin1Char(' ')), 0, false}, {QStringLiteral("%1 (%2)").arg(k).arg(disk), double(k), false}, {num(active, 2), active, true},
                                {group(avail), double(avail), true}, {group(size), double(size), true}, {num(queue, 2), queue, true}}});
        }
        m_storage->update(rows);
    }

    void refreshListeners()
    {
        const bool ufwOn = keyValue(readFile(QStringLiteral("/etc/ufw/ufw.conf")), QStringLiteral("ENABLED"), QLatin1Char('=')).toLower() == QLatin1String("yes");
        QList<Table::Row> rows;
        for (const Listener &l : readListeners()) {
            if (!m_checked.isEmpty() && !m_checked.contains(QString::number(l.pid))) {
                continue;
            }
            const bool local = l.address.contains(QLatin1String("127.")) || l.address.endsWith(QLatin1String("::1"));
            const QString fw = local ? tr7("Allowed, not restricted (this computer only)")
                : ufwOn          ? tr7("Restricted by firewall rules")
                                 : tr7("Allowed, not restricted");
            rows.append({l.protocol + l.address + l.port + QString::number(l.pid),
                         {{l.image, 0, false}, {l.pid ? QString::number(l.pid) : QString(), double(l.pid), true}, {l.address, 0, false}, {l.port, l.port.toDouble(), true},
                          {l.protocol, 0, false}, {fw, 0, false}}});
        }
        m_listen->update(rows);
        m_sListen->setFilter(filterText());
    }

    /* ---- menus ---- */

    void processMenu(int pid, QPoint pos)
    {
        QMenu menu(this);
        const QString name = m_names.value(pid);
        const bool stopped = readFile(QStringLiteral("/proc/%1/stat").arg(pid)).section(QLatin1Char(')'), 1).trimmed().startsWith(QLatin1Char('T'));
        menu.addAction(tr7("End Process"), this, [this, pid, name] {
            if (QMessageBox::question(this, tr7("Resource Monitor"), tr7("End process \"%1\"?").arg(name)) == QMessageBox::Yes) {
                ::kill(pid, SIGTERM);
            }
        });
        menu.addAction(tr7("End Process Tree"), this, [this, pid, name] {
            if (QMessageBox::question(this, tr7("Resource Monitor"), tr7("End \"%1\" and all the processes it started?").arg(name)) == QMessageBox::Yes) {
                QList<int> todo{pid};
                QList<int> all;
                while (!todo.isEmpty()) {
                    const int p = todo.takeFirst();
                    all << p;
                    for (const Proc &c : std::as_const(m_lastProcs)) {
                        if (c.ppid == p) {
                            todo << c.pid;
                        }
                    }
                }
                for (int p : all) {
                    ::kill(p, SIGTERM);
                }
            }
        });
        menu.addSeparator();
        menu.addAction(stopped ? tr7("Resume Process") : tr7("Suspend Process"), this, [pid, stopped] {
            ::kill(pid, stopped ? SIGCONT : SIGSTOP);
        });
        menu.addSeparator();
        menu.addAction(tr7("Search Online"), this, [name] {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.google.com/search?q=") + QUrl::toPercentEncoding(name)));
        });
        menu.exec(pos);
    }

    void serviceMenu(const QString &name, QPoint pos)
    {
        QMenu menu(this);
        auto ctl = [this, name](const QString &verb) {
            QProcess::startDetached(QStringLiteral("pkexec"), {QStringLiteral("systemctl"), verb, name + QStringLiteral(".service")});
            QTimer::singleShot(2500, this, [this] {
                refreshServices();
            });
        };
        const bool running = m_services->rowText(name, 3) == tr7("Running", "service");
        QAction *start = menu.addAction(tr7("Start Service"), this, [ctl] {
            ctl(QStringLiteral("start"));
        });
        QAction *stop = menu.addAction(tr7("Stop Service"), this, [ctl] {
            ctl(QStringLiteral("stop"));
        });
        QAction *restart = menu.addAction(tr7("Restart Service"), this, [ctl] {
            ctl(QStringLiteral("restart"));
        });
        start->setEnabled(!running);
        stop->setEnabled(running);
        restart->setEnabled(running);
        menu.addSeparator();
        menu.addAction(tr7("Search Online"), this, [name] {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.google.com/search?q=") + QUrl::toPercentEncoding(name + QStringLiteral(" service"))));
        });
        menu.exec(pos);
    }

    int m_ncpu = 1;
    long m_hz = 100;
    QAction *m_stop, *m_start;
    QTabBar *m_tabs;
    QSplitter *m_split;
    QStackedWidget *m_left;
    GraphPane *m_graphs;
    RoundButton *m_showGraphs;
    QTimer *m_timer, *m_slow;
    QElapsedTimer m_clock, m_serviceClock;

    QList<Table *> m_procTables;
    QSet<QString> m_checked;
    QHash<int, QString> m_names;
    QHash<int, QList<double>> m_cpuHistory;
    QList<Proc> m_lastProcs;
    QList<Socket> m_lastSockets;
    QList<CpuTimes> m_prevCpu;
    QList<DiskStat> m_prevDisks;
    QList<NetDev> m_prevNet;
    QHash<QString, double> m_activeTime;
    QHash<QString, double> m_queueLen;
    quint64 m_prevMajfault = 0;
    QHash<QString, quint64> m_prevServices;
    QHash<QString, QList<double>> m_serviceHistory;
    double m_servicesCpu = 0;
    QHash<int, QList<QPair<double, double>>> m_diskHist, m_netHist;
    QHash<QString, QList<QPair<double, double>>> m_addrHist;
    QHash<QString, QPair<QString, int>> m_addrWho;

    Table *m_ovCpu, *m_ovDisk, *m_ovNet, *m_ovMem;
    Table *m_cpuProcs, *m_services = nullptr, *m_handles, *m_modules;
    Table *m_memProcs;
    Table *m_diskProcs, *m_diskActivity, *m_storage;
    Table *m_netProcs, *m_netActivity, *m_tcp, *m_listen;
    Section *m_sServices, *m_sHandles, *m_sModules, *m_sDiskActivity, *m_sNetActivity, *m_sTcp, *m_sListen;
    QLineEdit *m_search;
    MemoryBar *m_memBar;
    Indicator *m_iOvCpu, *m_iOvFreq, *m_iOvDisk, *m_iOvDiskTime, *m_iOvNet, *m_iOvNetUse, *m_iOvFaults, *m_iOvMemUse;
    Indicator *m_iCpuProcs, *m_iCpuFreq, *m_iServices, *m_iMemUsed, *m_iMemInUse, *m_iMemAvail, *m_iDiskIo, *m_iDiskTime, *m_iNetIo, *m_iNetUse;
    ResGraph *m_gOvCpu, *m_gOvDisk, *m_gOvNet, *m_gOvMem;
    ResGraph *m_gCpuTotal, *m_gCpuServices;
    QList<ResGraph *> m_gCpuCores;
    ResGraph *m_gMemUsed, *m_gMemCommit, *m_gMemFaults;
    ResGraph *m_gDisk;
    QHash<QString, ResGraph *> m_gQueues;
    ResGraph *m_gNet, *m_gTcp;
    QHash<QString, ResGraph *> m_gIfaces;
};
}
