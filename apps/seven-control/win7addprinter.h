/*
 * "Add Printer" wizard of Windows 7 (Devices and Printers > Add a printer), on top of CUPS.
 *
 *   local printer    -> choose a port (LPT, COM, USB printers lpinfo sees, PDF) or a new TCP/IP / IPP port
 *   network printer  -> printers lpinfo finds (DNS-SD, SNMP), a shared printer by name, or a host name / IP
 *   driver           -> manufacturer / model lists from `lpinfo -m`, "Windows Update" installs the free
 *                       driver packages, "Have Disk..." takes .ppd files from a folder
 *   install          -> lpadmin -p QUEUE -E -v URI -m MODEL|-P FILE -D NAME, then sharing and the default printer
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7chrome.h"
#include "win7programs.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QCollator>
#include <QComboBox>
#include <QDirIterator>
#include <QGridLayout>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>

namespace Win7Printer
{
using Win7Programs::ArrowLink;
using Win7Programs::pageTitle;
using Win7Programs::wrapped;

struct Driver {
    QString make;  // "Brother"
    QString model; // "Brother DCP-116C Foomatic/..."
    QString ppd;   // lpinfo name for -m, or a file for -P
    bool file = false;
};

/** "RICOH" and "Ricoh" are one manufacturer; short all-capital words (HP, OKI) stay as they are */
inline QString makeName(const QString &raw)
{
    QString m = raw.trimmed();
    if (m.compare(QLatin1String("Hewlett-Packard"), Qt::CaseInsensitive) == 0 || m.compare(QLatin1String("Hewlett Packard"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("HP");
    }
    if (m.startsWith(QLatin1String("Kyocera"), Qt::CaseInsensitive)) {
        return QStringLiteral("Kyocera");
    }
    QStringList words = m.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (QString &w : words) {
        if (!(w.size() <= 3 && w == w.toUpper())) {
            w = w.left(1).toUpper() + w.mid(1).toLower();
        }
    }
    return words.join(QLatin1Char(' '));
}

/** `lpinfo -l -m`: one entry per model, the manufacturer from the IEEE 1284 device id */
inline QList<Driver> parseDrivers(const QString &lpinfo)
{
    QList<Driver> list;
    QHash<QString, int> seen; // shown text -> index, English drivers win over other languages
    QString name, lang, makeModel, deviceId;
    auto flush = [&] {
        if (name.isEmpty() || name == QLatin1String("everywhere") || makeModel.isEmpty()) {
            name.clear();
            return;
        }
        static const QRegularExpression mfg(QStringLiteral("(?:^|;)\\s*(?:MFG|MANUFACTURER):([^;]+)"));
        QString make = mfg.match(deviceId).captured(1);
        if (make.isEmpty()) {
            make = makeModel.section(QLatin1Char(' '), 0, 0);
        }
        Driver d;
        d.make = makeName(make);
        d.ppd = name;
        d.model = makeModel.startsWith(d.make, Qt::CaseInsensitive) || makeModel.startsWith(make, Qt::CaseInsensitive) ? makeModel : d.make + QLatin1Char(' ') + makeModel;
        const QString key = d.model.toLower();
        if (seen.contains(key)) {
            if (lang.startsWith(QLatin1String("en"))) {
                list[seen.value(key)] = d;
            }
        } else {
            seen.insert(key, list.size());
            list.append(d);
        }
        name.clear();
    };
    for (const QString &raw : lpinfo.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("Model:"))) {
            flush();
            name = line.section(QLatin1Char('='), 1).trimmed();
            lang.clear();
            makeModel.clear();
            deviceId.clear();
        } else if (line.startsWith(QLatin1String("natural_language = "))) {
            lang = line.mid(19);
        } else if (line.startsWith(QLatin1String("make-and-model = "))) {
            makeModel = line.mid(17);
        } else if (line.startsWith(QLatin1String("device-id = "))) {
            deviceId = line.mid(12);
        }
    }
    flush();
    return list;
}

/** what lpinfo -l -v reports: real devices only (with a "scheme:/..." URI) */
struct Found {
    QString uri;
    QString cls;  // direct / network / serial
    QString info; // "EPSON L3270 Series"
    QString makeModel;
};

inline QList<Found> parseDevices(const QString &text)
{
    QList<Found> out;
    Found cur;
    auto flush = [&] {
        if (cur.uri.contains(QLatin1String(":/"))) {
            out.append(cur);
        }
        cur = Found();
    };
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("Device: uri = "))) {
            flush();
            cur.uri = line.mid(14);
        } else if (line.startsWith(QLatin1String("class = "))) {
            cur.cls = line.mid(8);
        } else if (line.startsWith(QLatin1String("info = "))) {
            cur.info = line.mid(7);
        } else if (line.startsWith(QLatin1String("make-and-model = "))) {
            cur.makeModel = line.mid(17);
        }
    }
    flush();
    return out;
}

/** CUPS queue names: letters, digits, _ and -, not taken yet */
inline QString queueName(const QString &display)
{
    QString q = display;
    q.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_"));
    q.remove(QRegularExpression(QStringLiteral("^_+|_+$")));
    if (q.isEmpty()) {
        q = QStringLiteral("Printer");
    }
    const QStringList taken = Win7Programs::run(QStringLiteral("lpstat"), {QStringLiteral("-e")}).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QString name = q;
    for (int i = 2; taken.contains(name, Qt::CaseInsensitive); ++i) {
        name = QStringLiteral("%1_%2").arg(q).arg(i);
    }
    return name;
}

/** *Manufacturer / *NickName of the .ppd files in a folder ("Have Disk...") */
inline QList<Driver> disksDrivers(const QString &where)
{
    QList<Driver> out;
    QStringList files;
    const QFileInfo fi(where);
    if (fi.isFile()) {
        files.append(fi.absoluteFilePath());
    } else {
        QDirIterator it(where, {QStringLiteral("*.ppd"), QStringLiteral("*.PPD"), QStringLiteral("*.ppd.gz")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext() && files.size() < 500) {
            files.append(it.next());
        }
    }
    static const QRegularExpression key(QStringLiteral("^\\*(Manufacturer|NickName|ModelName):\\s*\"([^\"]*)\""), QRegularExpression::MultilineOption);
    for (const QString &f : files) {
        QByteArray data;
        if (f.endsWith(QLatin1String(".gz"))) {
            QProcess p;
            p.start(QStringLiteral("zcat"), {f});
            p.waitForFinished(5000);
            data = p.readAllStandardOutput().left(65536);
        } else {
            QFile file(f);
            if (file.open(QIODevice::ReadOnly)) {
                data = file.read(65536);
            }
        }
        Driver d;
        d.ppd = f;
        d.file = true;
        QString nick, model;
        for (auto m = key.globalMatch(QString::fromLatin1(data)); m.hasNext();) {
            const auto x = m.next();
            if (x.captured(1) == QLatin1String("Manufacturer")) {
                d.make = makeName(x.captured(2));
            } else if (x.captured(1) == QLatin1String("NickName")) {
                nick = x.captured(2);
            } else {
                model = x.captured(2);
            }
        }
        d.model = !nick.isEmpty() ? nick : !model.isEmpty() ? model : QFileInfo(f).completeBaseName();
        if (d.make.isEmpty()) {
            d.make = makeName(d.model.section(QLatin1Char(' '), 0, 0));
        }
        out.append(d);
    }
    return out;
}

/** "Install From Disk": the folder with the manufacturer's files */
inline QString haveDisk(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr7("Install From Disk"));
    dlg.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dlg.setFixedSize(470, 230);
    auto *g = new QGridLayout(&dlg);
    g->setContentsMargins(16, 16, 14, 16);
    g->setHorizontalSpacing(12);
    auto *ic = new QLabel;
    ic->setPixmap(QIcon::fromTheme(QStringLiteral("seven-printer-disk")).pixmap(32, 32));
    g->addWidget(ic, 0, 0, Qt::AlignTop);
    g->addWidget(wrapped(tr7("Insert the manufacturer's installation disk, and then make sure that the correct drive is selected below.")), 0, 1, Qt::AlignTop);
    auto *buttons = new QVBoxLayout;
    auto *ok = new QPushButton(QStringLiteral("OK"));
    auto *cancel = new QPushButton(tr7("Cancel"));
    ok->setDefault(true);
    for (QPushButton *b : {ok, cancel}) {
        b->setMinimumWidth(85);
        buttons->addWidget(b);
    }
    buttons->addStretch(1);
    g->addLayout(buttons, 0, 2, 2, 1);
    g->setRowStretch(1, 1);
    g->addWidget(new QLabel(tr7("Copy manufacturer's files from:")), 2, 1);
    auto *path = new QComboBox;
    path->setEditable(true);
    const QString media = QStringLiteral("/media/") + qEnvironmentVariable("USER");
    for (const QString &d : QDir(media).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        path->addItem(media + QLatin1Char('/') + d);
    }
    path->addItem(QDir::homePath() + QStringLiteral("/Downloads"));
    g->addWidget(path, 3, 1);
    auto *browse = new QPushButton(tr7("Browse..."));
    browse->setMinimumWidth(85);
    g->addWidget(browse, 3, 2);
    QObject::connect(browse, &QPushButton::clicked, &dlg, [&dlg, path] {
        const QString f = QFileDialog::getOpenFileName(&dlg, tr7("Locate File"), path->currentText(),
                                                       tr7("Setup Information (*.ppd *.PPD *.ppd.gz);;All files (*)"));
        if (!f.isEmpty()) {
            path->setEditText(f);
        }
    });
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    return dlg.exec() == QDialog::Accepted ? path->currentText().trimmed() : QString();
}

class AddPrinterWizard : public QDialog
{
public:
    enum Page { TypePage, PortPage, TcpPage, SearchPage, ByNamePage, DriverPage, NamePage, InstallPage, SharePage, DonePage };

    AddPrinterWizard(QWidget *parent, std::function<void()> changed)
        : QDialog(parent)
        , m_changed(std::move(changed))
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(tr7("Add a printer"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-printer-add")));
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        resize(640, 500);
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        auto *white = new QWidget;
        white->setObjectName(QStringLiteral("win7wizwhite"));
        white->setAttribute(Qt::WA_StyledBackground, true);
        white->setStyleSheet(QStringLiteral("#win7wizwhite { background: #ffffff; }"));
        auto *wv = new QVBoxLayout(white);
        wv->setContentsMargins(10, 8, 30, 14);
        auto *head = new QHBoxLayout;
        m_back = new QToolButton;
        m_back->setIcon(QIcon::fromTheme(QStringLiteral("go-previous")));
        m_back->setAutoRaise(true);
        m_back->setEnabled(false);
        head->addWidget(m_back);
        auto *hicon = new QLabel;
        hicon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-printer-add")).pixmap(16, 16));
        head->addWidget(hicon);
        head->addWidget(new QLabel(tr7("Add a printer")), 1);
        wv->addLayout(head);
        m_stack = new QStackedWidget;
        m_stack->setContentsMargins(30, 14, 0, 0);
        wv->addWidget(m_stack, 1);
        outer->addWidget(white, 1);
        auto *line = new QFrame;
        line->setFixedHeight(1);
        line->setStyleSheet(QStringLiteral("background: #dfdfdf;"));
        outer->addWidget(line);
        auto *btnRow = new QHBoxLayout;
        btnRow->setContentsMargins(10, 10, 10, 10);
        btnRow->addStretch(1);
        m_next = new QPushButton(tr7("Next"));
        m_cancel = new QPushButton(tr7("Cancel"));
        for (QPushButton *b : {m_next, m_cancel}) {
            b->setMinimumWidth(75);
            btnRow->addWidget(b);
        }
        m_next->setDefault(true);
        outer->addLayout(btnRow);

        for (int p = TypePage; p <= DonePage; ++p) {
            m_stack->addWidget(build(Page(p)));
        }
        connect(m_back, &QToolButton::clicked, this, [this] { back(); });
        connect(m_next, &QPushButton::clicked, this, [this] { next(); });
        connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
        go(TypePage, false);
        loadDrivers(); // takes a few seconds, so it starts right away
    }

private:
    static QWidget *page(QVBoxLayout *&v, const QString &title)
    {
        auto *w = new QWidget;
        v = new QVBoxLayout(w);
        v->setContentsMargins(30, 10, 0, 0);
        v->setSpacing(10);
        v->addWidget(pageTitle(title));
        v->addSpacing(6);
        return w;
    }

    QWidget *build(Page p)
    {
        QVBoxLayout *v = nullptr;
        switch (p) {
        case TypePage: {
            QWidget *w = page(v, tr7("What type of printer do you want to install?"));
            v->addWidget(new ArrowLink(tr7("Add a local printer"),
                                       tr7("Use this option only if you don't have a USB printer. (Windows automatically installs USB printers when you plug them in.)"),
                                       [this] {
                                           m_network = false;
                                           go(PortPage);
                                       }));
            v->addSpacing(8);
            v->addWidget(new ArrowLink(tr7("Add a network, wireless or Bluetooth printer"),
                                       tr7("Make sure that your computer is connected to the network, or that your Bluetooth or wireless printer is turned on."),
                                       [this] {
                                           m_network = true;
                                           go(SearchPage);
                                       }));
            v->addStretch(1);
            return w;
        }
        case PortPage: {
            QWidget *w = page(v, tr7("Choose a printer port"));
            v->addWidget(wrapped(tr7("A printer port is a type of connection that allows your computer to exchange information with a printer.")));
            auto *g = new QGridLayout;
            g->setHorizontalSpacing(12);
            g->setVerticalSpacing(12);
            m_useExisting = new QRadioButton(tr7("Use an existing port:"));
            m_useExisting->setChecked(true);
            m_createPort = new QRadioButton(tr7("Create a new port:"));
            m_ports = new QComboBox;
            m_portType = new QComboBox;
            m_portType->addItem(tr7("Standard TCP/IP Port"), QStringLiteral("socket"));
            m_portType->addItem(tr7("IPP Port (Internet Printing)"), QStringLiteral("ipp"));
            m_portType->addItem(tr7("LPD Port"), QStringLiteral("lpd"));
            m_portType->setEnabled(false);
            g->addWidget(m_useExisting, 0, 0);
            g->addWidget(m_ports, 0, 1);
            g->addWidget(m_createPort, 1, 0);
            auto *typeLabel = new QLabel(tr7("Type of port:"));
            typeLabel->setContentsMargins(40, 0, 0, 0);
            typeLabel->setEnabled(false);
            g->addWidget(typeLabel, 2, 0);
            g->addWidget(m_portType, 2, 1);
            g->setColumnStretch(1, 1);
            v->addLayout(g);
            connect(m_createPort, &QRadioButton::toggled, this, [this, typeLabel](bool on) {
                m_ports->setEnabled(!on);
                m_portType->setEnabled(on);
                typeLabel->setEnabled(on);
            });
            v->addStretch(1);
            fillPorts();
            return w;
        }
        case TcpPage: {
            QWidget *w = page(v, tr7("Type a printer hostname or IP address"));
            auto *g = new QGridLayout;
            g->setHorizontalSpacing(12);
            g->setVerticalSpacing(10);
            m_deviceType = new QComboBox;
            m_deviceType->addItem(tr7("Autodetect"), QStringLiteral("auto"));
            m_deviceType->addItem(tr7("TCP/IP Device"), QStringLiteral("socket"));
            m_deviceType->addItem(tr7("Web Services Device (IPP)"), QStringLiteral("ipp"));
            m_deviceType->addItem(tr7("LPD Device"), QStringLiteral("lpd"));
            m_host = new QLineEdit;
            m_portName = new QLineEdit;
            m_query = new QCheckBox(tr7("Query the printer and automatically select the driver to use"));
            m_query->setChecked(true);
            g->addWidget(new QLabel(tr7("Device type:")), 0, 0);
            g->addWidget(m_deviceType, 0, 1);
            g->addWidget(new QLabel(tr7("Hostname or IP address:")), 1, 0);
            g->addWidget(m_host, 1, 1);
            g->addWidget(new QLabel(tr7("Port name:")), 2, 0);
            g->addWidget(m_portName, 2, 1);
            g->setColumnStretch(1, 1);
            v->addLayout(g);
            v->addWidget(m_query);
            connect(m_host, &QLineEdit::textChanged, this, [this](const QString &t) {
                m_portName->setText(t.trimmed());
                m_next->setEnabled(!t.trimmed().isEmpty());
            });
            connect(m_deviceType, &QComboBox::currentIndexChanged, this, [this] {
                m_query->setEnabled(m_deviceType->currentData().toString() != QLatin1String("socket") && m_deviceType->currentData().toString() != QLatin1String("lpd"));
            });
            v->addStretch(1);
            return w;
        }
        case SearchPage: {
            QWidget *w = page(v, tr7("Searching for available printers..."));
            m_searchTitle = qobject_cast<QLabel *>(v->itemAt(0)->widget());
            m_found = new QTreeWidget;
            m_found->setHeaderLabels({tr7("Printer Name"), tr7("Address")});
            m_found->setRootIsDecorated(false);
            m_found->setColumnWidth(0, 260);
            m_found->setIconSize(QSize(32, 32));
            v->addWidget(m_found, 1);
            m_searchRow = new QWidget;
            auto *row = new QHBoxLayout(m_searchRow);
            row->setContentsMargins(0, 0, 0, 0);
            m_searchBar = new QProgressBar;
            m_searchBar->setRange(0, 0);
            m_searchBar->setTextVisible(false);
            m_searchBar->setFixedHeight(14);
            row->addWidget(m_searchBar, 1);
            m_stop = new QPushButton(tr7("Stop"));
            row->addWidget(m_stop);
            v->addWidget(m_searchRow);
            v->addSpacing(6);
            v->addWidget(new Link(tr7("The printer that I want isn't listed"), [this] {
                stopSearch();
                go(ByNamePage);
            }));
            connect(m_stop, &QPushButton::clicked, this, [this] { stopSearch(); });
            connect(m_found, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) { m_next->setEnabled(it); });
            connect(m_found, &QTreeWidget::itemDoubleClicked, this, [this] { next(); });
            return w;
        }
        case ByNamePage: {
            QWidget *w = page(v, tr7("Find a printer by name or TCP/IP address"));
            m_byShare = new QRadioButton(tr7("Select a shared printer by name"));
            m_byShare->setChecked(true);
            v->addWidget(m_byShare);
            m_share = new QLineEdit;
            m_share->setContentsMargins(22, 0, 0, 0);
            v->addWidget(m_share);
            auto *ex = new QLabel(tr7("Example: \\\\computername\\printername or\nhttp://computername/printers/printername/.printer"));
            ex->setContentsMargins(22, 0, 0, 0);
            ex->setStyleSheet(QStringLiteral("color: #555555;"));
            v->addWidget(ex);
            v->addSpacing(6);
            m_byAddress = new QRadioButton(tr7("Add a printer using a TCP/IP address or hostname"));
            v->addWidget(m_byAddress);
            connect(m_byShare, &QRadioButton::toggled, this, [this](bool on) {
                m_share->setEnabled(on);
                updateNext();
            });
            connect(m_share, &QLineEdit::textChanged, this, [this] { updateNext(); });
            v->addStretch(1);
            return w;
        }
        case DriverPage: {
            QWidget *w = page(v, tr7("Install the printer driver"));
            auto *intro = new QHBoxLayout;
            auto *ic = new QLabel;
            ic->setPixmap(QIcon::fromTheme(QStringLiteral("seven-printer-driver")).pixmap(32, 32));
            intro->addWidget(ic, 0, Qt::AlignTop);
            intro->addWidget(wrapped(tr7("Choose your printer from the list. Click Windows Update to see more models.\n\nTo install the driver from an installation CD, click Have Disk.")),
                             1);
            v->addLayout(intro);
            auto *lists = new QHBoxLayout;
            m_makes = new QTreeWidget;
            m_makes->setHeaderLabels({tr7("Manufacturer")});
            m_makes->setRootIsDecorated(false);
            m_makes->setFixedWidth(200);
            m_models = new QTreeWidget;
            m_models->setHeaderLabels({tr7("Printers")});
            m_models->setRootIsDecorated(false);
            m_models->setUniformRowHeights(true);
            lists->addWidget(m_makes);
            lists->addWidget(m_models, 1);
            v->addLayout(lists, 1);
            auto *bottom = new QHBoxLayout;
            auto *sig = new QVBoxLayout;
            auto *sigRow = new QHBoxLayout;
            m_sigIcon = new QLabel;
            m_sigIcon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-driver-signed")).pixmap(16, 16));
            sigRow->addWidget(m_sigIcon);
            m_sigText = new QLabel(tr7("This driver is digitally signed."));
            sigRow->addWidget(m_sigText, 1);
            sig->addLayout(sigRow);
            sig->addWidget(new Link(tr7("Tell me why driver signing is important"), [this] {
                QMessageBox::information(this, tr7("Driver signing"),
                                         tr7("The drivers in the list come with the system or are installed from its package store, so they are verified.\n\nDrivers installed from a manufacturer's disk (.ppd files) are not checked by the system - install them only from trusted sources."));
            }));
            bottom->addLayout(sig, 1);
            auto *buttons = new QVBoxLayout;
            m_update = new QPushButton(tr7("Windows Update"));
            auto *disk = new QPushButton(tr7("Have Disk..."));
            for (QPushButton *b : {m_update, disk}) {
                b->setMinimumWidth(200);
                buttons->addWidget(b);
            }
            bottom->addLayout(buttons);
            v->addLayout(bottom);
            connect(m_makes, &QTreeWidget::currentItemChanged, this, [this] { fillModels(); });
            connect(m_models, &QTreeWidget::currentItemChanged, this, [this] { updateNext(); });
            connect(m_models, &QTreeWidget::itemDoubleClicked, this, [this] { next(); });
            connect(m_update, &QPushButton::clicked, this, [this] { windowsUpdate(); });
            connect(disk, &QPushButton::clicked, this, [this] {
                const QString where = haveDisk(this);
                if (where.isEmpty()) {
                    return;
                }
                const QList<Driver> found = disksDrivers(where);
                if (found.isEmpty()) {
                    QMessageBox::warning(this, tr7("Install From Disk"),
                                         tr7("The specified location does not contain information about your printer (.ppd files)."));
                    return;
                }
                m_diskDrivers = found;
                fillMakes();
            });
            return w;
        }
        case NamePage: {
            QWidget *w = page(v, tr7("Type a printer name"));
            auto *row = new QHBoxLayout;
            row->addWidget(new QLabel(tr7("Printer name:")));
            m_name = new QLineEdit;
            row->addWidget(m_name, 1);
            v->addLayout(row);
            v->addSpacing(8);
            m_nameNote = wrapped(QString());
            v->addWidget(m_nameNote);
            connect(m_name, &QLineEdit::textChanged, this, [this] { updateNext(); });
            v->addStretch(1);
            return w;
        }
        case InstallPage: {
            QWidget *w = page(v, tr7("Installing printer..."));
            m_installText = wrapped(QString());
            v->addWidget(m_installText);
            m_installBar = new QProgressBar;
            m_installBar->setRange(0, 0);
            m_installBar->setTextVisible(false);
            v->addWidget(m_installBar);
            v->addStretch(1);
            return w;
        }
        case SharePage: {
            QWidget *w = page(v, tr7("Printer Sharing"));
            v->addWidget(wrapped(tr7("If you want to share this printer, you must provide a share name. You can use the suggested name or type a new one. The share name will be visible to other network users.")));
            m_noShare = new QRadioButton(tr7("Do not share this printer"));
            m_doShare = new QRadioButton(tr7("Share this printer so that others on your network can find and use it"));
            m_noShare->setChecked(true);
            v->addWidget(m_noShare);
            v->addWidget(m_doShare);
            auto *g = new QGridLayout;
            g->setContentsMargins(22, 0, 0, 0);
            m_shareName = new QLineEdit;
            m_location = new QLineEdit;
            m_comment = new QLineEdit;
            g->addWidget(new QLabel(tr7("Share name:")), 0, 0);
            g->addWidget(m_shareName, 0, 1);
            g->addWidget(new QLabel(tr7("Location:")), 1, 0);
            g->addWidget(m_location, 1, 1);
            g->addWidget(new QLabel(tr7("Comment:")), 2, 0);
            g->addWidget(m_comment, 2, 1);
            auto *fields = new QWidget;
            fields->setLayout(g);
            fields->setEnabled(false);
            v->addWidget(fields);
            connect(m_doShare, &QRadioButton::toggled, fields, &QWidget::setEnabled);
            v->addStretch(1);
            return w;
        }
        case DonePage: {
            QWidget *w = page(v, QString());
            m_doneTitle = qobject_cast<QLabel *>(v->itemAt(0)->widget());
            m_default = new QCheckBox(tr7("Set as the default printer"));
            v->addWidget(m_default);
            v->addSpacing(10);
            v->addWidget(wrapped(tr7("To check if your printer is working properly, or to see troubleshooting information for the printer, print a test page.")));
            auto *test = new QPushButton(tr7("Print a test page"));
            v->addWidget(test, 0, Qt::AlignLeft);
            connect(test, &QPushButton::clicked, this, [this] {
                const QString page = QFile::exists(QStringLiteral("/usr/share/cups/data/testprint")) ? QStringLiteral("/usr/share/cups/data/testprint")
                                                                                                      : QStringLiteral("/usr/share/cups/data/default-testpage.pdf");
                QProcess::startDetached(QStringLiteral("lp"), {QStringLiteral("-d"), m_queue, QStringLiteral("-t"), tr7("Test page"), page});
                QMessageBox::information(this, m_name->text(),
                                         tr7("A test page has been sent to the printer \"%1\".\n\nThis test page briefly demonstrates the printer's ability to print graphics and text, and it provides technical information about the printer.")
                                             .arg(m_name->text()));
            });
            v->addStretch(1);
            return w;
        }
        }
        return new QWidget;
    }

    /* ---------- navigation ---------- */

    void go(Page p, bool remember = true)
    {
        if (remember && m_stack->currentIndex() != p) {
            m_history.append(Page(m_stack->currentIndex()));
        }
        m_stack->setCurrentIndex(p);
        m_back->setEnabled(!m_history.isEmpty() && p != InstallPage && p != SharePage && p != DonePage);
        m_next->setText(p == DonePage ? tr7("Done") : tr7("Next"));
        m_next->setVisible(p != TypePage);
        m_cancel->setVisible(p != DonePage);
        m_cancel->setEnabled(p != InstallPage);
        if (p == SearchPage) {
            startSearch();
        } else if (p == TcpPage) {
            m_host->setFocus();
        } else if (p == DriverPage) {
            fillMakes();
        } else if (p == NamePage) {
            m_name->setText(m_driver.model.section(QLatin1Char(','), 0, 0).remove(QRegularExpression(QStringLiteral("\\s+(Foomatic|CUPS\\+Gutenprint|-\\s).*$"))).trimmed());
            if (m_name->text().isEmpty()) {
                m_name->setText(m_suggestedName);
            }
            m_name->selectAll();
            m_name->setFocus();
            m_nameNote->setText(tr7("This printer will be installed with the %1 driver.").arg(m_driver.model));
        } else if (p == SharePage) {
            m_shareName->setText(m_name->text());
        }
        updateNext();
    }

    void back()
    {
        if (m_history.isEmpty()) {
            return;
        }
        stopSearch();
        const Page p = m_history.takeLast();
        go(p, false);
    }

    void updateNext()
    {
        bool ok = true;
        switch (m_stack->currentIndex()) {
        case TcpPage:
            ok = !m_host->text().trimmed().isEmpty();
            break;
        case SearchPage:
            ok = m_found->currentItem() != nullptr;
            break;
        case ByNamePage:
            ok = !m_byShare->isChecked() || !m_share->text().trimmed().isEmpty();
            break;
        case DriverPage:
            ok = m_models->currentItem() != nullptr;
            break;
        case NamePage:
            ok = !m_name->text().trimmed().isEmpty();
            break;
        case InstallPage:
            ok = false;
            break;
        default:
            break;
        }
        m_next->setEnabled(ok);
    }

    void next()
    {
        switch (m_stack->currentIndex()) {
        case PortPage:
            if (m_createPort->isChecked()) {
                m_deviceType->setCurrentIndex(qMax(0, m_deviceType->findData(m_portType->currentData())));
                go(TcpPage);
            } else {
                m_uri = m_ports->currentData().toString();
                m_direct = true;
                m_suggestedName = m_ports->currentData(Qt::UserRole + 1).toString();
                go(DriverPage);
                preselect(m_ports->currentData(Qt::UserRole + 1).toString());
            }
            return;
        case TcpPage: {
            const QString host = m_host->text().trimmed();
            const QString type = m_deviceType->currentData().toString();
            m_suggestedName = host;
            if (type == QLatin1String("socket")) {
                m_uri = QStringLiteral("socket://%1:9100").arg(host);
            } else if (type == QLatin1String("lpd")) {
                m_uri = QStringLiteral("lpd://%1/queue").arg(host);
            } else {
                m_uri = QStringLiteral("ipp://%1/ipp/print").arg(host);
            }
            m_direct = false;
            if (type != QLatin1String("socket") && type != QLatin1String("lpd") && m_query->isChecked()) {
                useDriverless();
                return;
            }
            go(DriverPage);
            return;
        }
        case SearchPage: {
            QTreeWidgetItem *it = m_found->currentItem();
            if (!it) {
                return;
            }
            stopSearch();
            m_uri = it->data(0, Qt::UserRole).toString();
            m_direct = false;
            m_suggestedName = it->text(0);
            if (m_uri.startsWith(QLatin1String("dnssd:")) || m_uri.startsWith(QLatin1String("ipp")) || m_uri.startsWith(QLatin1String("driverless:"))) {
                useDriverless();
            } else {
                go(DriverPage);
                preselect(it->data(0, Qt::UserRole + 1).toString());
            }
            return;
        }
        case ByNamePage: {
            if (m_byAddress->isChecked()) {
                go(TcpPage);
                return;
            }
            QString s = m_share->text().trimmed();
            if (s.startsWith(QLatin1String("\\\\"))) { // \\computer\printer
                s = QStringLiteral("smb://") + s.mid(2).replace(QLatin1Char('\\'), QLatin1Char('/'));
            }
            if (s.startsWith(QLatin1String("http://")) || s.startsWith(QLatin1String("https://"))) {
                QUrl u(s);
                QString path = u.path();
                path.remove(QRegularExpression(QStringLiteral("/\\.printer$")));
                s = QStringLiteral("%1://%2%3%4").arg(u.scheme() == QLatin1String("https") ? QStringLiteral("ipps") : QStringLiteral("ipp"), u.host(),
                                                     u.port() > 0 ? QStringLiteral(":%1").arg(u.port()) : QString(), path);
            }
            if (!s.contains(QLatin1String("://"))) {
                QMessageBox::warning(this, windowTitle(), tr7("Windows couldn't find the printer \"%1\". Check the name and try again.").arg(m_share->text()));
                return;
            }
            m_uri = s;
            m_direct = false;
            m_suggestedName = s.section(QLatin1Char('/'), -1);
            if (s.startsWith(QLatin1String("ipp"))) {
                useDriverless();
            } else {
                go(DriverPage);
            }
            return;
        }
        case DriverPage: {
            QTreeWidgetItem *it = m_models->currentItem();
            if (!it) {
                return;
            }
            const int i = it->data(0, Qt::UserRole).toInt();
            m_driver = it->data(0, Qt::UserRole + 1).toBool() ? m_diskDrivers.at(i) : m_drivers.at(i);
            go(NamePage);
            return;
        }
        case NamePage:
            install();
            return;
        case SharePage:
            applySharing();
            go(DonePage);
            m_doneTitle->setText(tr7("You've successfully added %1").arg(m_name->text()));
            m_default->setChecked(!Win7Programs::run(QStringLiteral("lpstat"), {QStringLiteral("-d")}).contains(QLatin1Char(':')));
            return;
        case DonePage:
            if (m_default->isChecked()) {
                QProcess::execute(QStringLiteral("lpoptions"), {QStringLiteral("-d"), m_queue});
            }
            if (m_changed) {
                m_changed();
            }
            accept();
            return;
        default:
            return;
        }
    }

    /** IPP Everywhere: the printer describes itself, no driver to pick */
    void useDriverless()
    {
        m_direct = false;
        m_driver = Driver{QString(), tr7("IPP Everywhere™ (no separate driver)"), QStringLiteral("everywhere"), false};
        go(NamePage);
    }

    /* ---------- ports ---------- */

    void fillPorts()
    {
        m_ports->clear();
        for (int i = 0; i < 3; ++i) {
            m_ports->addItem(tr7("LPT%1: (Printer Port)").arg(i + 1), QStringLiteral("parallel:/dev/lp%1").arg(i));
        }
        for (int i = 0; i < 4; ++i) {
            m_ports->addItem(tr7("COM%1: (Serial Port)").arg(i + 1), QStringLiteral("serial:/dev/ttyS%1?baud=9600").arg(i));
        }
        if (QFile::exists(QStringLiteral("/usr/lib/cups/backend/cups-pdf"))) {
            m_ports->addItem(tr7("PDF: (Print to PDF file)"), QStringLiteral("cups-pdf:/"));
        }
        // USB printers lpinfo sees right now come as "USB001 (Virtual printer port for USB)"
        auto *proc = new QProcess(this);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
        proc->setProcessEnvironment(env);
        connect(proc, &QProcess::finished, this, [this, proc] {
            int n = 1;
            for (const Found &f : parseDevices(QString::fromUtf8(proc->readAllStandardOutput()))) {
                if (!f.uri.startsWith(QLatin1String("usb:")) && !f.uri.startsWith(QLatin1String("hp:"))) {
                    continue;
                }
                const QString name = f.makeModel.isEmpty() || f.makeModel == QLatin1String("Unknown") ? f.info : f.makeModel;
                m_ports->addItem(tr7("USB%1 (Virtual printer port for USB) - %2").arg(n++, 3, 10, QLatin1Char('0')).arg(name), f.uri);
                m_ports->setItemData(m_ports->count() - 1, name, Qt::UserRole + 1);
            }
            proc->deleteLater();
        });
        proc->start(QStringLiteral("lpinfo"), {QStringLiteral("-l"), QStringLiteral("--include-schemes"), QStringLiteral("usb,hp,parallel,serial"), QStringLiteral("-v")});
    }

    /* ---------- network search ---------- */

    void startSearch()
    {
        m_found->clear();
        m_searchTitle->setText(tr7("Searching for available printers..."));
        m_searchRow->show();
        m_stop->setEnabled(true);
        if (m_search) {
            return;
        }
        m_search = new QProcess(this);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
        m_search->setProcessEnvironment(env);
        connect(m_search, &QProcess::finished, this, [this] {
            const QStringList installed = Win7Programs::run(QStringLiteral("lpstat"), {QStringLiteral("-v")}).split(QLatin1Char('\n'));
            for (const Found &f : parseDevices(QString::fromUtf8(m_search->readAllStandardOutput()))) {
                if (f.cls != QLatin1String("network") && !f.uri.startsWith(QLatin1String("bluetooth"))) {
                    continue;
                }
                bool known = false;
                for (const QString &l : installed) {
                    known = known || l.endsWith(f.uri);
                }
                QString name = f.makeModel.isEmpty() || f.makeModel == QLatin1String("Unknown") ? f.info : f.makeModel;
                if (name.isEmpty()) {
                    name = QUrl::fromPercentEncoding(QUrl(f.uri).host().toUtf8()).section(QLatin1Char('.'), 0, 0);
                }
                QString address = QUrl(f.uri).host();
                if (f.uri.startsWith(QLatin1String("dnssd:"))) {
                    address = QUrl::fromPercentEncoding(f.uri.mid(8).section(QLatin1Char('/'), 0, 0).toUtf8());
                }
                auto *it = new QTreeWidgetItem(m_found, {name + (known ? tr7(" (already installed)") : QString()), address});
                it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-dev-printer")));
                it->setData(0, Qt::UserRole, f.uri);
                it->setData(0, Qt::UserRole + 1, f.makeModel);
            }
            m_search->deleteLater();
            m_search = nullptr;
            searchDone();
        });
        m_search->start(QStringLiteral("lpinfo"), {QStringLiteral("-l"), QStringLiteral("--include-schemes"), QStringLiteral("dnssd,snmp,bluetooth"), QStringLiteral("--timeout"),
                                                  QStringLiteral("10"), QStringLiteral("-v")});
    }

    void stopSearch()
    {
        if (m_search) {
            m_search->disconnect(this);
            m_search->kill();
            m_search->deleteLater();
            m_search = nullptr;
        }
        searchDone();
    }

    void searchDone()
    {
        m_searchRow->hide();
        m_stop->setEnabled(false);
        m_searchTitle->setText(m_found->topLevelItemCount() ? tr7("Select a printer") : tr7("No printers were found"));
    }

    /* ---------- drivers ---------- */

    void loadDrivers()
    {
        static QList<Driver> cache;
        if (!cache.isEmpty()) {
            m_drivers = cache;
            return;
        }
        auto *proc = new QProcess(this);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
        proc->setProcessEnvironment(env);
        connect(proc, &QProcess::finished, this, [this, proc] {
            cache = parseDrivers(QString::fromUtf8(proc->readAllStandardOutput()));
            m_drivers = cache;
            proc->deleteLater();
            if (m_stack->currentIndex() == DriverPage) {
                fillMakes();
                preselect(m_pendingMatch);
            }
        });
        proc->start(QStringLiteral("lpinfo"), {QStringLiteral("-l"), QStringLiteral("-m")});
    }

    const QList<Driver> &shownDrivers() const
    {
        return m_diskDrivers.isEmpty() ? m_drivers : m_diskDrivers;
    }

    void fillMakes()
    {
        const QString keep = m_makes->currentItem() ? m_makes->currentItem()->text(0) : QString();
        m_makes->clear();
        m_models->clear();
        if (shownDrivers().isEmpty()) {
            m_makes->addTopLevelItem(new QTreeWidgetItem({tr7("Please wait...")}));
            m_makes->topLevelItem(0)->setFlags(Qt::NoItemFlags);
            return;
        }
        QStringList makes;
        for (const Driver &d : shownDrivers()) {
            if (m_direct && d.ppd.startsWith(QLatin1String("driverless:"))) {
                continue;
            }
            if (!makes.contains(d.make)) {
                makes.append(d.make);
            }
        }
        QCollator coll(QLocale(QLocale::English));
        std::sort(makes.begin(), makes.end(), [&coll](const QString &a, const QString &b) {
            return coll.compare(a, b) < 0;
        });
        for (const QString &m : makes) {
            m_makes->addTopLevelItem(new QTreeWidgetItem({m}));
        }
        const bool disk = !m_diskDrivers.isEmpty();
        m_sigIcon->setPixmap(QIcon::fromTheme(disk ? QStringLiteral("seven-driver-unsigned") : QStringLiteral("seven-driver-signed")).pixmap(16, 16));
        m_sigText->setText(disk ? tr7("This driver is not digitally signed!") : tr7("This driver is digitally signed."));
        const auto match = m_makes->findItems(keep, Qt::MatchExactly);
        m_makes->setCurrentItem(match.isEmpty() ? m_makes->topLevelItem(0) : match.first());
    }

    void fillModels()
    {
        m_models->clear();
        QTreeWidgetItem *mk = m_makes->currentItem();
        if (!mk) {
            return;
        }
        const bool disk = !m_diskDrivers.isEmpty();
        const QIcon icon = QIcon::fromTheme(disk ? QStringLiteral("seven-driver-unsigned") : QStringLiteral("seven-driver-signed"));
        QList<QTreeWidgetItem *> items;
        const QList<Driver> &list = shownDrivers();
        for (int i = 0; i < list.size(); ++i) {
            if (list.at(i).make != mk->text(0)) {
                continue;
            }
            if (m_direct && list.at(i).ppd.startsWith(QLatin1String("driverless:"))) {
                continue; // these talk IPP; a USB / LPT / COM port wants a real driver
            }
            auto *it = new QTreeWidgetItem({list.at(i).model});
            it->setIcon(0, icon);
            it->setData(0, Qt::UserRole, i);
            it->setData(0, Qt::UserRole + 1, disk);
            items.append(it);
        }
        QCollator coll(QLocale(QLocale::English));
        coll.setNumericMode(true);
        std::sort(items.begin(), items.end(), [&coll](QTreeWidgetItem *a, QTreeWidgetItem *b) {
            return coll.compare(a->text(0), b->text(0)) < 0;
        });
        m_models->addTopLevelItems(items);
        updateNext();
    }

    /** selects the manufacturer and the closest model for a device ("EPSON L3270 Series") */
    void preselect(const QString &makeModel)
    {
        m_pendingMatch = makeModel;
        if (makeModel.isEmpty() || shownDrivers().isEmpty()) {
            return;
        }
        const QString make = makeName(makeModel.section(QLatin1Char(' '), 0, 0));
        // "EPSON L3270 Series": the make, then the first word of the model
        const auto mk = m_makes->findItems(make, Qt::MatchFixedString);
        if (mk.isEmpty()) {
            return;
        }
        m_makes->setCurrentItem(mk.first());
        const QString model = makeModel.section(QLatin1Char(' '), 1, 1);
        if (model.isEmpty()) {
            return;
        }
        // "EPSON L3270 Series, Epson Inkjet Printer Driver (ESC/P-R)" or a "(recommended)" one before the rest
        QTreeWidgetItem *best = nullptr;
        for (int pass = 0; pass < 2 && !best; ++pass) {
            for (int i = 0; i < m_models->topLevelItemCount(); ++i) {
                const QString t = m_models->topLevelItem(i)->text(0);
                const QRegularExpression word(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(model)), QRegularExpression::CaseInsensitiveOption);
                if (!word.match(t).hasMatch()) {
                    continue;
                }
                if (pass == 1 || t.contains(QLatin1String("ESC/P-R")) || t.contains(QLatin1String("recommended"))) {
                    best = m_models->topLevelItem(i);
                    break;
                }
            }
        }
        if (best) {
            m_models->setCurrentItem(best);
            m_models->scrollToItem(best);
        }
    }

    /** "Windows Update": the free printer driver packages, then a fresh list */
    void windowsUpdate()
    {
        const QStringList wanted{QStringLiteral("printer-driver-all"), QStringLiteral("printer-driver-escpr"), QStringLiteral("printer-driver-gutenprint"),
                                 QStringLiteral("openprinting-ppds"), QStringLiteral("hplip")};
        QStringList missing;
        const QHash<QString, Win7Programs::Fields> status = Win7Programs::dpkgStatus();
        for (const QString &p : wanted) {
            if (!status.contains(p)) {
                missing.append(p);
            }
        }
        m_update->setEnabled(false);
        auto reload = [this] {
            m_drivers.clear();
            m_diskDrivers.clear();
            fillMakes();
            QProcess p;
            p.start(QStringLiteral("lpinfo"), {QStringLiteral("-l"), QStringLiteral("-m")});
            p.waitForFinished(60000);
            m_drivers = parseDrivers(QString::fromUtf8(p.readAllStandardOutput()));
            fillMakes();
            m_update->setEnabled(true);
        };
        if (missing.isEmpty()) {
            reload();
            return;
        }
        Win7Programs::runTask(this, tr7("Add a printer"), QStringLiteral("seven-printer-add"),
                              tr7("Please wait while Windows updates the list of printers. This might take several minutes."), QStringLiteral("pkexec"),
                              {QStringLiteral("/bin/sh"), QStringLiteral("-c"), Win7Programs::aptScript(missing, {})},
                              [this, reload](bool ok, bool cancelled, const QString &out) {
                                  if (!ok && !cancelled) {
                                      Win7Programs::showFailure(this, windowTitle(), tr7("Could not update the list of printers."), out);
                                  }
                                  reload();
                              });
    }

    /* ---------- install ---------- */

    void install()
    {
        m_queue = queueName(m_name->text().trimmed());
        go(InstallPage);
        m_installText->setText(tr7("Please wait while the printer \"%1\" is installed.").arg(m_name->text()));
        QStringList args{QStringLiteral("-p"), m_queue, QStringLiteral("-E"), QStringLiteral("-v"), m_uri, QStringLiteral("-D"), m_name->text().trimmed()};
        if (m_driver.file) {
            args << QStringLiteral("-P") << m_driver.ppd;
        } else {
            args << QStringLiteral("-m") << m_driver.ppd;
        }
        auto *proc = new QProcess(this);
        proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(proc, &QProcess::finished, this, [this, proc](int code) {
            const QString out = QString::fromLocal8Bit(proc->readAll()).trimmed();
            proc->deleteLater();
            if (code == 0) {
                m_history.clear();
                go(SharePage);
                return;
            }
            // driverless needs a printer that answers; otherwise the driver is picked by hand
            if (m_driver.ppd == QLatin1String("everywhere")) {
                QMessageBox::information(this, windowTitle(),
                                         tr7("Windows could not detect the driver for this printer automatically. Select the driver from the list."));
                m_stack->setCurrentIndex(NamePage);
                go(DriverPage);
                return;
            }
            Win7Programs::showFailure(this, windowTitle(), tr7("The printer could not be installed."), out);
            m_stack->setCurrentIndex(NamePage);
            go(NamePage, false);
        });
        proc->start(QStringLiteral("lpadmin"), args);
    }

    void applySharing()
    {
        const bool share = m_doShare->isChecked();
        QStringList args{QStringLiteral("-p"), m_queue, QStringLiteral("-o"), share ? QStringLiteral("printer-is-shared=true") : QStringLiteral("printer-is-shared=false")};
        if (share && !m_location->text().trimmed().isEmpty()) {
            args << QStringLiteral("-L") << m_location->text().trimmed();
        }
        QProcess::execute(QStringLiteral("lpadmin"), args);
        if (share) {
            QProcess::execute(QStringLiteral("cupsctl"), {QStringLiteral("--share-printers")});
        }
    }

    std::function<void()> m_changed;
    QStackedWidget *m_stack;
    QToolButton *m_back;
    QPushButton *m_next;
    QPushButton *m_cancel;
    QList<Page> m_history;
    bool m_network = false;
    bool m_direct = false; // USB / LPT / COM: no driverless (IPP) drivers
    // port
    QRadioButton *m_useExisting;
    QRadioButton *m_createPort;
    QComboBox *m_ports;
    QComboBox *m_portType;
    // TCP/IP
    QComboBox *m_deviceType;
    QLineEdit *m_host;
    QLineEdit *m_portName;
    QCheckBox *m_query;
    // search
    QLabel *m_searchTitle;
    QTreeWidget *m_found;
    QWidget *m_searchRow;
    QProgressBar *m_searchBar;
    QPushButton *m_stop;
    QProcess *m_search = nullptr;
    // by name
    QRadioButton *m_byShare;
    QRadioButton *m_byAddress;
    QLineEdit *m_share;
    // driver
    QTreeWidget *m_makes;
    QTreeWidget *m_models;
    QLabel *m_sigIcon;
    QLabel *m_sigText;
    QPushButton *m_update;
    QList<Driver> m_drivers;
    QList<Driver> m_diskDrivers;
    QString m_pendingMatch;
    // name / install / share / done
    QLineEdit *m_name;
    QLabel *m_nameNote;
    QLabel *m_installText;
    QProgressBar *m_installBar;
    QRadioButton *m_noShare;
    QRadioButton *m_doShare;
    QLineEdit *m_shareName;
    QLineEdit *m_location;
    QLineEdit *m_comment;
    QLabel *m_doneTitle;
    QCheckBox *m_default;
    QString m_uri;
    QString m_suggestedName;
    QString m_queue;
    Driver m_driver;
};

inline void addPrinterWizard(QWidget *parent, std::function<void()> changed = {})
{
    (new AddPrinterWizard(parent, std::move(changed)))->show();
}
} // namespace Win7Printer
