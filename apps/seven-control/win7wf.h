/*
 * "Windows Firewall with Advanced Security" (wf.msc) of Windows 7 on top of ufw.
 *
 *   rules       -> read through KDE's ufw helper (org.kde.ufw.query, no password), changed with pkexec ufw
 *   profiles    -> ufw has one set of rules for every network: one "Загальний профіль"
 *   names       -> ufw rules have no names: rules made here keep theirs in ~/.config/seven-firewallrc
 *   not here    -> connection security (IPsec) rules and per-program rules: Linux firewalls filter ports
 *
 * Icons are the ones of AuthFWSnapin.dll (Windows 7).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7wrapmenubar.h"
#include "win7states.h"
#include "win7diskmgmt.h"
#include "win7eventvwr.h"
#include "win7firewall.h"

#include <QFileDialog>
#include <QInputDialog>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>

namespace Win7Wf
{
using Win7Event::ActionGroup;
using Win7Event::ActionItem;
using Win7Event::ActionPane;
using Win7Event::DarkHeader;
using Win7Event::PaneFrame;
using Win7Event::Section;
using Win7Event::VistaHeader;
using Win7Firewall::Rule;
using Win7Firewall::Ufw;

inline QString actionName(const QString &a)
{
    if (a == QLatin1String("allow")) {
        return tr7("Allow");
    }
    if (a == QLatin1String("limit")) {
        return tr7("Allow (rate limited)");
    }
    return tr7("Block");
}

inline QString any(const QString &v, const QString &word = tr7("Any", "address"))
{
    return v.isEmpty() || v == QLatin1String("any") || v == QLatin1String("0.0.0.0/0") || v == QLatin1String("::/0") ? word : v;
}

/** the ufw command line that describes a rule, used to delete it (both IPv4 and IPv6 copies) or to re-add it */
inline QString ruleSpec(const Rule &r, const QString &action = QString())
{
    QString s = (action.isEmpty() ? r.action : action) + QLatin1Char(' ') + r.direction;
    if (!r.dapp.isEmpty()) {
        return s + QStringLiteral(" from %1 to %2 app %3").arg(any(r.src, QStringLiteral("any")), any(r.dst, QStringLiteral("any")), Win7Firewall::quoted(r.dapp));
    }
    if (!r.protocol.isEmpty() && r.protocol != QLatin1String("any")) {
        s += QStringLiteral(" proto ") + r.protocol;
    }
    s += QStringLiteral(" from ") + any(r.src, QStringLiteral("any"));
    if (!r.sport.isEmpty() && r.sport != QLatin1String("any")) {
        s += QStringLiteral(" port ") + r.sport;
    }
    s += QStringLiteral(" to ") + any(r.dst, QStringLiteral("any"));
    if (!r.dport.isEmpty() && r.dport != QLatin1String("any")) {
        s += QStringLiteral(" port ") + r.dport;
    }
    return s;
}

inline KConfigGroup ruleNames()
{
    return KConfigGroup(KSharedConfig::openConfig(QStringLiteral("seven-firewallrc")), QStringLiteral("Rules"));
}

inline QString ruleKey(const Rule &r)
{
    return ruleSpec(r, QStringLiteral("x")).mid(2);
}

/** a readable name: the one given here, the feature of the "Allowed programs" list, or the port */
inline QString ruleName(const Rule &r)
{
    const QString own = ruleNames().readEntry(ruleKey(r), QString());
    if (!own.isEmpty()) {
        return own;
    }
    for (const Win7Firewall::Feature &f : Win7Firewall::features()) {
        if (!f.app.isEmpty() && f.app == r.dapp) {
            return f.name;
        }
        for (const QString &spec : f.portSpecs) {
            if (Win7Firewall::ruleMatches(r, spec)) {
                return f.name;
            }
        }
    }
    if (!r.dapp.isEmpty()) {
        return r.dapp;
    }
    if (!r.dport.isEmpty() && r.dport != QLatin1String("any")) {
        return tr7("Port %1%2").arg(r.dport, r.protocol.isEmpty() || r.protocol == QLatin1String("any") ? QString() : QLatin1Char('/') + r.protocol.toUpper());
    }
    return tr7("Rule %1").arg(r.position);
}

inline QString arrows()
{
    auto make = [](bool open) {
        const QString path = QDir::tempPath() + QStringLiteral("/seven-wf-arrow-%1-%2.png").arg(open ? QStringLiteral("open") : QStringLiteral("closed")).arg(getuid());
        QImage img(16, 16, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        if (open) {
            p.setPen(QPen(QColor(0x26, 0x26, 0x26), 1));
            p.setBrush(QColor(0x59, 0x59, 0x59));
            p.drawPolygon(QPolygonF({QPointF(10.5, 5.5), QPointF(10.5, 10.5), QPointF(5.5, 10.5)}));
        } else {
            p.setPen(QPen(QColor(0x94, 0x94, 0x94), 1));
            p.setBrush(Qt::white);
            p.drawPolygon(QPolygonF({QPointF(6.5, 4.5), QPointF(10.5, 8.5), QPointF(6.5, 12.5)}));
        }
        p.end();
        img.save(path);
        return path;
    };
    return QStringLiteral("QTreeView::branch { background: white; border-image: none; image: none; }"
                          "QTreeView::branch:has-children:closed { image: url(%1); }"
                          "QTreeView::branch:has-children:open { image: url(%2); }")
        .arg(make(false), make(true));
}

/* ---------------- "New Inbound Rule Wizard" ---------------- */

/** the classic wizard of wf.msc: header band, "Steps" on the left, < Back / Next > / Cancel */
class RuleWizard : public QDialog
{
public:
    RuleWizard(QWidget *parent, bool inbound)
        : QDialog(parent)
        , m_inbound(inbound)
    {
        setWindowTitle(inbound ? tr7("New Inbound Rule Wizard") : tr7("New Outbound Rule Wizard"));
        setWindowIcon(QIcon::fromTheme(inbound ? QStringLiteral("seven-wf-newinbound") : QStringLiteral("seven-wf-newoutbound")));
        setFixedSize(700, 540);
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        auto *head = new QWidget;
        head->setObjectName(QStringLiteral("win7wfhead"));
        head->setAttribute(Qt::WA_StyledBackground, true);
        head->setStyleSheet(QStringLiteral("#win7wfhead { background: white; border-bottom: 1px solid #a0a0a0; }"));
        head->setFixedHeight(66);
        auto *hh = new QHBoxLayout(head);
        hh->setContentsMargins(14, 8, 14, 8);
        auto *hv = new QVBoxLayout;
        m_title = new QLabel;
        QFont bf = m_title->font();
        bf.setBold(true);
        m_title->setFont(bf);
        m_subtitle = new QLabel;
        m_subtitle->setContentsMargins(14, 0, 0, 0);
        m_subtitle->setWordWrap(true);
        hv->addWidget(m_title);
        hv->addWidget(m_subtitle);
        hh->addLayout(hv, 1);
        auto *hicon = new QLabel;
        hicon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-wf-firewall")).pixmap(48, 48));
        hh->addWidget(hicon);
        outer->addWidget(head);
        auto *body = new QHBoxLayout;
        body->setContentsMargins(14, 12, 14, 8);
        body->setSpacing(14);
        auto *stepsBox = new QVBoxLayout;
        auto *stepsTitle = new QLabel(tr7("Steps:"));
        stepsTitle->setFont(bf);
        stepsBox->addWidget(stepsTitle);
        m_steps = new QListWidget;
        m_steps->setFixedWidth(170);
        m_steps->setFrameShape(QFrame::NoFrame);
        m_steps->setFocusPolicy(Qt::NoFocus);
        m_steps->setSelectionMode(QAbstractItemView::NoSelection);
        m_steps->setStyleSheet(QStringLiteral("QListWidget { background: transparent; } QListWidget::item { padding: 2px 0px; }"));
        stepsBox->addWidget(m_steps, 1);
        body->addLayout(stepsBox);
        m_stack = new QStackedWidget;
        body->addWidget(m_stack, 1);
        outer->addLayout(body, 1);
        auto *row = new QHBoxLayout;
        row->setContentsMargins(14, 8, 14, 12);
        row->addStretch(1);
        m_back = new QPushButton(tr7("< Back"));
        m_next = new QPushButton(tr7("Next >"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        for (QPushButton *b : {m_back, m_next, cancel}) {
            b->setMinimumWidth(80);
            row->addWidget(b);
        }
        m_next->setDefault(true);
        outer->addLayout(row);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_back, &QPushButton::clicked, this, [this] {
            if (!m_history.isEmpty()) {
                show(m_history.takeLast(), false);
            }
        });
        connect(m_next, &QPushButton::clicked, this, [this] {
            next();
        });
        buildType();
        buildPorts();
        buildPredefined();
        buildCustom();
        buildAction();
        buildName();
        show(TypePage, false);
    }

    QString command() const
    {
        return m_command;
    }
    QString ruleTitle() const
    {
        return m_name->text().trimmed();
    }

private:
    enum { TypePage, PortsPage, PredefinedPage, CustomPage, ActionPage, NamePage };

    QVBoxLayout *page(int id)
    {
        auto *w = new QWidget;
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(8);
        m_stack->insertWidget(id, w);
        return v;
    }

    void buildType()
    {
        auto *v = page(TypePage);
        v->addWidget(new QLabel(tr7("What type of rule would you like to create?")));
        v->addSpacing(8);
        m_port = new QRadioButton(tr7("Port", "rule type"));
        m_predefined = new QRadioButton(tr7("Predefined:"));
        m_custom = new QRadioButton(tr7("Custom", "rule type"));
        m_port->setChecked(true);
        auto desc = [](const QString &t) {
            auto *l = new QLabel(t);
            l->setContentsMargins(22, 0, 0, 0);
            l->setWordWrap(true);
            return l;
        };
        v->addWidget(m_port);
        v->addWidget(desc(tr7("Rule that controls connections for a TCP or UDP port.")));
        v->addSpacing(6);
        v->addWidget(m_predefined);
        m_feature = new QComboBox;
        for (const Win7Firewall::Feature &f : Win7Firewall::features()) {
            m_feature->addItem(f.name);
        }
        m_feature->setEnabled(false);
        auto *fl = new QHBoxLayout;
        fl->setContentsMargins(22, 0, 0, 0);
        fl->addWidget(m_feature, 1);
        v->addLayout(fl);
        v->addWidget(desc(tr7("Rule that controls connections for a well-known service.")));
        v->addSpacing(6);
        v->addWidget(m_custom);
        v->addWidget(desc(tr7("Custom rule: protocol, ports and addresses.")));
        v->addStretch(1);
        connect(m_predefined, &QRadioButton::toggled, m_feature, &QWidget::setEnabled);
        if (!m_inbound) {
            m_predefined->setEnabled(false); // the known services are about incoming connections
        }
    }

    void buildPorts()
    {
        auto *v = page(PortsPage);
        v->addWidget(new QLabel(tr7("Does this rule apply to TCP or UDP?")));
        m_tcp = new QRadioButton(tr7("TCP"));
        m_udp = new QRadioButton(tr7("UDP"));
        m_tcp->setChecked(true);
        auto *g1 = new QButtonGroup(this);
        g1->addButton(m_tcp);
        g1->addButton(m_udp);
        for (QRadioButton *b : {m_tcp, m_udp}) {
            b->setContentsMargins(14, 0, 0, 0);
            v->addWidget(b);
        }
        v->addSpacing(14);
        auto *which = new QLabel(m_inbound ? tr7("Does this rule apply to all local ports or specific local ports?")
                                           : tr7("Does this rule apply to all remote ports or specific remote ports?"));
        which->setWordWrap(true);
        v->addWidget(which);
        m_allPorts = new QRadioButton(m_inbound ? tr7("All local ports") : tr7("All remote ports"));
        m_somePorts = new QRadioButton(m_inbound ? tr7("Specific local ports:") : tr7("Specific remote ports:"));
        m_somePorts->setChecked(true);
        auto *g2 = new QButtonGroup(this);
        g2->addButton(m_allPorts);
        g2->addButton(m_somePorts);
        v->addWidget(m_allPorts);
        auto *row = new QHBoxLayout;
        row->addWidget(m_somePorts);
        m_ports = new QLineEdit;
        row->addWidget(m_ports, 1);
        v->addLayout(row);
        auto *ex = new QLabel(tr7("Example: 80, 443, 5000:5010"));
        ex->setContentsMargins(150, 0, 0, 0);
        v->addWidget(ex);
        v->addStretch(1);
        connect(m_somePorts, &QRadioButton::toggled, m_ports, &QWidget::setEnabled);
    }

    void buildPredefined()
    {
        auto *v = page(PredefinedPage);
        v->addWidget(new QLabel(tr7("The following rules will be created:")));
        m_preview = new QTreeWidget;
        m_preview->setRootIsDecorated(false);
        m_preview->setHeaderLabels({tr7("Rule"), tr7("Ports", "rules")});
        m_preview->setColumnWidth(0, 230);
        v->addWidget(m_preview, 1);
    }

    void buildCustom()
    {
        auto *v = page(CustomPage);
        auto *g = new QGridLayout;
        g->setVerticalSpacing(10);
        m_proto = new QComboBox;
        m_proto->addItem(tr7("Any"), QString());
        m_proto->addItem(QStringLiteral("TCP"), QStringLiteral("tcp"));
        m_proto->addItem(QStringLiteral("UDP"), QStringLiteral("udp"));
        m_customPorts = new QLineEdit;
        m_customPorts->setPlaceholderText(tr7("All Ports"));
        m_customPorts->setEnabled(false);
        m_remote = new QLineEdit;
        m_remote->setPlaceholderText(tr7("Any IP address"));
        g->addWidget(new QLabel(tr7("Protocol type:")), 0, 0);
        g->addWidget(m_proto, 0, 1);
        g->addWidget(new QLabel(m_inbound ? tr7("Local port:") : tr7("Remote port:")), 1, 0);
        g->addWidget(m_customPorts, 1, 1);
        g->addWidget(new QLabel(tr7("Remote IP address:")), 2, 0);
        g->addWidget(m_remote, 2, 1);
        g->setColumnStretch(1, 1);
        v->addWidget(new QLabel(tr7("To which ports and IP addresses does this rule apply?")));
        v->addSpacing(6);
        v->addLayout(g);
        auto *ex = new QLabel(tr7("An IP address can be given with a mask, for example 192.168.1.0/24."));
        ex->setWordWrap(true);
        v->addWidget(ex);
        v->addStretch(1);
        connect(m_proto, &QComboBox::currentIndexChanged, this, [this] {
            m_customPorts->setEnabled(!m_proto->currentData().toString().isEmpty());
        });
    }

    void buildAction()
    {
        auto *v = page(ActionPage);
        v->addWidget(new QLabel(tr7("What action should be taken when a connection matches the specified conditions?")));
        v->addSpacing(8);
        m_allow = new QRadioButton(tr7("Allow the connection"));
        m_limit = new QRadioButton(tr7("Allow, but limit the connection rate"));
        m_block = new QRadioButton(tr7("Block the connection"));
        m_allow->setChecked(true);
        auto desc = [](const QString &t) {
            auto *l = new QLabel(t);
            l->setContentsMargins(22, 0, 0, 6);
            l->setWordWrap(true);
            return l;
        };
        v->addWidget(m_allow);
        v->addWidget(desc(tr7("This includes connections that are protected with IPsec as well as those that are not.")));
        v->addWidget(m_limit);
        v->addWidget(desc(tr7("The connection is blocked if more than 6 attempts come from one address within 30 seconds (protection against password guessing).")));
        v->addWidget(m_block);
        v->addStretch(1);
        if (!m_inbound) {
            m_limit->setEnabled(false);
        }
    }

    void buildName()
    {
        auto *v = page(NamePage);
        v->addWidget(new QLabel(tr7("Name:")));
        m_name = new QLineEdit;
        v->addWidget(m_name);
        v->addWidget(new QLabel(tr7("Description (optional):")));
        m_desc = new QPlainTextEdit;
        m_desc->setFixedHeight(100);
        v->addWidget(m_desc);
        v->addStretch(1);
        connect(m_name, &QLineEdit::textChanged, this, [this] {
            m_next->setEnabled(m_stack->currentIndex() != NamePage || !m_name->text().trimmed().isEmpty());
        });
    }

    QList<int> flow() const
    {
        if (m_predefined->isChecked()) {
            return {TypePage, PredefinedPage, NamePage};
        }
        if (m_custom->isChecked()) {
            return {TypePage, CustomPage, ActionPage, NamePage};
        }
        return {TypePage, PortsPage, ActionPage, NamePage};
    }

    void show(int id, bool push)
    {
        if (push) {
            m_history.append(m_stack->currentIndex());
        }
        m_stack->setCurrentIndex(id);
        static const QHash<int, QPair<QString, QString>> heads{
            {TypePage, {tr7("Rule Type"), tr7("Specify the type of firewall rule to create.")}},
            {PortsPage, {tr7("Protocol and Ports"), tr7("Specify the protocols and ports to which this rule applies.")}},
            {PredefinedPage, {tr7("Predefined Rules"), tr7("Select the rules to be created.")}},
            {CustomPage, {tr7("Protocol and Ports"), tr7("Specify the protocols, ports and IP addresses to which this rule applies.")}},
            {ActionPage, {tr7("Action"), tr7("Specify the action to be taken when a connection matches the conditions specified in the rule.")}},
            {NamePage, {tr7("Name"), tr7("Specify the name and description of this rule.")}}};
        m_title->setText(heads.value(id).first);
        m_subtitle->setText(heads.value(id).second);
        m_steps->clear();
        for (int p : flow()) {
            auto *it = new QListWidgetItem((p == id ? QStringLiteral("●  ") : QStringLiteral("●  ")) + heads.value(p).first, m_steps);
            QFont f = it->font();
            f.setBold(p == id);
            it->setFont(f);
            it->setForeground(p == id ? QColor(0x00, 0x33, 0x99) : QColor(0x50, 0x50, 0x50));
        }
        m_back->setEnabled(id != TypePage);
        m_next->setText(id == NamePage ? tr7("Done") : tr7("Next >"));
        m_next->setEnabled(id != NamePage || !m_name->text().trimmed().isEmpty());
        if (id == PredefinedPage) {
            m_preview->clear();
            const Win7Firewall::Feature f = Win7Firewall::features().value(m_feature->currentIndex());
            new QTreeWidgetItem(m_preview, {f.name, f.app.isEmpty() ? f.portSpecs.join(QStringLiteral(", ")).toUpper() : tr7("profile ") + f.app});
            if (m_name->text().isEmpty()) {
                m_name->setText(f.name);
            }
        }
    }

    void next()
    {
        const QList<int> f = flow();
        const int cur = m_stack->currentIndex();
        if (cur == PortsPage && m_somePorts->isChecked()) {
            static const QRegularExpression valid(QStringLiteral("^\\d{1,5}(:\\d{1,5})?(,\\d{1,5}(:\\d{1,5})?)*$"));
            if (!valid.match(m_ports->text().remove(QLatin1Char(' '))).hasMatch()) {
                QMessageBox::warning(this, windowTitle(), tr7("Enter a port or a port range, for example 80, 443 or 5000:5010."));
                return;
            }
        }
        if (cur == NamePage) {
            build();
            accept();
            return;
        }
        const int i = f.indexOf(cur);
        show(f.value(i + 1, NamePage), true);
    }

    void build()
    {
        const QString action = m_block->isChecked() ? QStringLiteral("deny") : m_limit->isChecked() ? QStringLiteral("limit") : QStringLiteral("allow");
        const QString dir = m_inbound ? QStringLiteral("in") : QStringLiteral("out");
        const QString comment = QStringLiteral(" comment ") + Win7Firewall::quoted(QStringLiteral("Win7: ") + ruleTitle());
        if (m_predefined->isChecked()) {
            m_command = Win7Firewall::featureCommands(Win7Firewall::features().value(m_feature->currentIndex()), true).join(QStringLiteral(" && "));
            return;
        }
        if (m_custom->isChecked()) {
            const QString proto = m_proto->currentData().toString();
            const QString remote = m_remote->text().trimmed().isEmpty() ? QStringLiteral("any") : m_remote->text().trimmed();
            QString c = QStringLiteral("ufw %1 %2").arg(action, dir);
            if (!proto.isEmpty()) {
                c += QStringLiteral(" proto ") + proto;
            }
            const QString ports = m_customPorts->text().remove(QLatin1Char(' '));
            if (m_inbound) {
                c += QStringLiteral(" from ") + Win7Firewall::quoted(remote) + QStringLiteral(" to any");
                if (!proto.isEmpty() && !ports.isEmpty()) {
                    c += QStringLiteral(" port ") + ports;
                }
            } else {
                c += QStringLiteral(" from any to ") + Win7Firewall::quoted(remote);
                if (!proto.isEmpty() && !ports.isEmpty()) {
                    c += QStringLiteral(" port ") + ports;
                }
            }
            m_command = c + comment;
            return;
        }
        const QString proto = m_tcp->isChecked() ? QStringLiteral("tcp") : QStringLiteral("udp");
        QString c = QStringLiteral("ufw %1 %2 proto %3 from any to any").arg(action, dir, proto);
        if (m_somePorts->isChecked()) {
            c += QStringLiteral(" port ") + m_ports->text().remove(QLatin1Char(' '));
        }
        m_command = c + comment;
    }

    bool m_inbound;
    QLabel *m_title;
    QLabel *m_subtitle;
    QListWidget *m_steps;
    QStackedWidget *m_stack;
    QPushButton *m_back;
    QPushButton *m_next;
    QList<int> m_history;
    QRadioButton *m_port, *m_predefined, *m_custom;
    QComboBox *m_feature;
    QRadioButton *m_tcp, *m_udp, *m_allPorts, *m_somePorts;
    QLineEdit *m_ports;
    QTreeWidget *m_preview;
    QComboBox *m_proto;
    QLineEdit *m_customPorts;
    QLineEdit *m_remote;
    QRadioButton *m_allow, *m_limit, *m_block;
    QLineEdit *m_name;
    QPlainTextEdit *m_desc;
    QString m_command;
};

/* ---------------- the console ---------------- */

class FirewallConsole : public QMainWindow
{
public:
    FirewallConsole()
    {
        setWindowTitle(tr7("Windows Firewall with Advanced Security"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-wf-firewall")));
        resize(1060, 660);
        buildUi();
        reload();
        m_tree->setCurrentItem(m_tree->topLevelItem(0));
    }

private:
    enum Node { RootNode, InboundNode, OutboundNode, MonitoringNode, ActiveNode };

    void buildUi()
    {
        m_back = new QAction(Win7Disk::flatIcon(QStringLiteral("seven-mmc-back")), tr7("Back"), this);
        m_forward = new QAction(Win7Disk::flatIcon(QStringLiteral("seven-mmc-forward")), tr7("Forward"), this);
        m_back->setEnabled(false);
        m_forward->setEnabled(false);
        auto *console = new QAction(Win7Disk::flatIcon(QStringLiteral("seven-mmc-console")), tr7("Show/Hide Console Tree"), this);
        auto *help = new QAction(Win7Disk::flatIcon(QStringLiteral("seven-mmc-help")), tr7("Help"), this);
        auto *actionPane = new QAction(Win7Disk::flatIcon(QStringLiteral("seven-mmc-actionpane")), tr7("Show/Hide Action Pane"), this);
        console->setCheckable(true);
        console->setChecked(true);
        actionPane->setCheckable(true);
        actionPane->setChecked(true);
        connect(m_back, &QAction::triggered, this, [this] {
            navigate(-1);
        });
        connect(m_forward, &QAction::triggered, this, [this] {
            navigate(+1);
        });
        connect(help, &QAction::triggered, this, [this] {
            showHelp();
        });
        QMenu *file = menuBar()->addMenu(tr7("&File"));
        file->addAction(tr7("Exit"), this, &QWidget::close);
        m_actionMenu = menuBar()->addMenu(tr7("&Action"));
        connect(m_actionMenu, &QMenu::aboutToShow, this, [this] {
            m_actionMenu->clear();
            fillMenu(m_actionMenu, false);
        });
        QMenu *viewMenu = menuBar()->addMenu(tr7("&View"));
        viewMenu->addAction(tr7("Refresh"), this, [this] {
            reload();
        });
        QMenu *helpMenu = menuBar()->addMenu(tr7("&Help"));
        helpMenu->addAction(QIcon::fromTheme(QStringLiteral("seven-mmc-help")), tr7("Help"), this, [this] {
            showHelp();
        });
        QToolBar *bar = addToolBar(tr7("Standard"));
        bar->setMovable(false);
        bar->setIconSize(QSize(16, 16));
        bar->addAction(m_back);
        bar->addAction(m_forward);
        bar->addSeparator();
        bar->addAction(console);
        bar->addSeparator();
        bar->addAction(help);
        bar->addAction(actionPane);
        Win7Disk::mmcChrome(menuBar(), bar);
        win7WrapMenuBar(this); // Windows 7: the menu bar wraps instead of showing a "»" arrow

        m_tree = new QTreeWidget;
        m_tree->setHeaderHidden(true);
        m_tree->setIconSize(QSize(16, 16));
        m_tree->setFrameShape(QFrame::NoFrame);
        m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        m_tree->header()->setStretchLastSection(false);
        m_tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
        m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
        m_tree->setStyleSheet(QStringLiteral("QTreeView { background: white; outline: 0; show-decoration-selected: 0; }") + arrows()
                              + Win7States::itemViewSheet());
        auto *root = new QTreeWidgetItem(m_tree, {tr7("Windows Firewall with Advanced Security on Local Computer")});
        root->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-wf-firewall")));
        root->setData(0, Qt::UserRole, RootNode);
        auto add = [](QTreeWidgetItem *parent, const QString &t, const QString &icon, int node) {
            auto *it = new QTreeWidgetItem(parent, {t});
            it->setIcon(0, QIcon::fromTheme(icon));
            it->setData(0, Qt::UserRole, node);
            return it;
        };
        add(root, tr7("Inbound Rules"), QStringLiteral("seven-wf-inbound_ico"), InboundNode);
        add(root, tr7("Outbound Rules"), QStringLiteral("seven-wf-outbound_ico"), OutboundNode);
        auto *mon = add(root, tr7("Monitoring"), QStringLiteral("seven-wf-monitoring"), MonitoringNode);
        add(mon, tr7("Windows Firewall"), QStringLiteral("seven-wf-firewallrulesfolder"), ActiveNode);
        root->setExpanded(true);

        m_middle = new QStackedWidget;
        m_middle->addWidget(buildOverview());
        m_middle->addWidget(buildRulesPage());
        m_actions = new ActionPane;
        auto *actionScroll = new QScrollArea;
        actionScroll->setWidget(m_actions);
        actionScroll->setWidgetResizable(true);
        actionScroll->setFrameShape(QFrame::NoFrame);
        actionScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *split = new QSplitter(Qt::Horizontal);
        auto *treePane = new PaneFrame(m_tree, false);
        auto *actionsPane = new PaneFrame(actionScroll, true);
        split->addWidget(treePane);
        split->addWidget(new PaneFrame(m_middle, true));
        split->addWidget(actionsPane);
        split->setChildrenCollapsible(false);
        split->setStretchFactor(1, 1);
        split->setSizes({240, 600, 220});
        split->setHandleWidth(3);
        split->setStyleSheet(QStringLiteral("QSplitter::handle { background: #f0f0f0; }"));
        connect(console, &QAction::toggled, treePane, &QWidget::setVisible);
        connect(actionPane, &QAction::toggled, actionsPane, &QWidget::setVisible);
        auto *frame = new QWidget;
        frame->setObjectName(QStringLiteral("win7wfframe"));
        frame->setAttribute(Qt::WA_StyledBackground, true);
        frame->setStyleSheet(QStringLiteral("#win7wfframe { background: #f0f0f0; }"));
        auto *fv = new QVBoxLayout(frame);
        fv->setContentsMargins(0, 2, 0, 2);
        fv->addWidget(split);
        setCentralWidget(frame);
        statusBar()->setStyleSheet(QStringLiteral("QStatusBar { background: #f1eeec; } QStatusBar::item { border: none; }"));
        for (int w : {133, 85}) {
            auto *panel = new Win7Event::StatusPanel;
            panel->setFixedSize(w, 20);
            statusBar()->addPermanentWidget(panel);
        }
        connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it, QTreeWidgetItem *prev) {
            if (!it) {
                return;
            }
            if (!m_navigating && prev) {
                m_history.append(prev);
                m_future.clear();
            }
            m_back->setEnabled(!m_history.isEmpty());
            m_forward->setEnabled(!m_future.isEmpty());
            showNode();
        });
        connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
            if (QTreeWidgetItem *it = m_tree->itemAt(pos)) {
                m_tree->setCurrentItem(it);
                QMenu m;
                fillMenu(&m, true);
                m.exec(m_tree->viewport()->mapToGlobal(pos));
            }
        });
    }

    QWidget *pageWithHeader(DarkHeader *&h)
    {
        auto *w = new QWidget;
        w->setObjectName(QStringLiteral("win7wfpage"));
        w->setAttribute(Qt::WA_StyledBackground, true);
        w->setStyleSheet(QStringLiteral("#win7wfpage { background: white; }"));
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        h = new DarkHeader;
        v->addWidget(h);
        return w;
    }

    QWidget *buildOverview()
    {
        QWidget *page = pageWithHeader(m_overviewHeader);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        m_overview = new QWidget;
        m_overview->setObjectName(QStringLiteral("win7wfov"));
        m_overview->setAttribute(Qt::WA_StyledBackground, true);
        m_overview->setStyleSheet(QStringLiteral("#win7wfov { background: white; }"));
        scroll->setWidget(m_overview);
        page->layout()->addWidget(scroll);
        return page;
    }

    void fillOverview(bool monitoring)
    {
        delete m_overview->layout();
        qDeleteAll(m_overview->findChildren<QWidget *>(Qt::FindDirectChildrenOnly));
        auto *v = new QVBoxLayout(m_overview);
        v->setContentsMargins(10, 10, 10, 10);
        v->setSpacing(10);
        auto line = [](const QString &icon, const QString &text) {
            auto *row = new QHBoxLayout;
            row->setSpacing(6);
            auto *ic = new QLabel;
            ic->setPixmap(QIcon::fromTheme(icon).pixmap(16, 16));
            row->addWidget(ic, 0, Qt::AlignTop);
            auto *t = new QLabel(text);
            t->setWordWrap(true);
            row->addWidget(t, 1);
            return row;
        };
        // profile: ufw has one, so one block like Windows 7's active profile
        auto *profile = new QWidget;
        auto *pv = new QVBoxLayout(profile);
        pv->setContentsMargins(0, 0, 0, 0);
        pv->setSpacing(6);
        auto *ph = new QHBoxLayout;
        auto *shield = new QLabel;
        shield->setPixmap(QIcon::fromTheme(m_ufw.enabled ? QStringLiteral("seven-wf-shieldcheck") : QStringLiteral("seven-wf-shieldcritical")).pixmap(32, 32));
        ph->addWidget(shield, 0, Qt::AlignTop);
        auto *pt = new QLabel(tr7("<b>Public profile is Active</b>"));
        ph->addWidget(pt, 1);
        pv->addLayout(ph);
        auto *lines = new QVBoxLayout;
        lines->setContentsMargins(38, 0, 0, 0);
        lines->addLayout(line(m_ufw.enabled ? QStringLiteral("seven-wf-shieldcheck") : QStringLiteral("seven-wf-shieldcritical"),
                              m_ufw.enabled ? tr7("Windows Firewall is on.") : tr7("Windows Firewall is off.")));
        const bool inBlock = m_ufw.incoming != QLatin1String("allow");
        const bool outBlock = m_ufw.outgoing != QLatin1String("allow") && !m_ufw.outgoing.isEmpty();
        lines->addLayout(line(inBlock ? QStringLiteral("seven-wf-block") : QStringLiteral("seven-wf-allow"),
                              inBlock ? tr7("Inbound connections that do not match a rule are blocked.")
                                      : tr7("Inbound connections that do not match a rule are allowed.")));
        lines->addLayout(line(outBlock ? QStringLiteral("seven-wf-block") : QStringLiteral("seven-wf-allow"),
                              outBlock ? tr7("Outbound connections that do not match a rule are blocked.")
                                       : tr7("Outbound connections that do not match a rule are allowed.")));
        pv->addLayout(lines);
        auto *props = new Link(tr7("Windows Firewall Properties"), [this] {
            firewallProperties();
        });
        props->setContentsMargins(38, 4, 0, 0);
        pv->addWidget(props);
        if (!m_ufw.ok) {
            pv->addWidget(new QLabel(tr7("The firewall state could not be read.")));
        }
        auto *intro = new QLabel(monitoring ? tr7("This shows the current firewall state and the rules in effect now.")
                                            : tr7("Windows Firewall with Advanced Security provides network security for Windows computers."));
        intro->setWordWrap(true);
        v->addWidget(intro);
        v->addWidget(new Section(monitoring ? tr7("Firewall State") : tr7("Overview"), profile));
        if (!monitoring) {
            auto *start = new QWidget;
            auto *sv = new QVBoxLayout(start);
            sv->setContentsMargins(0, 0, 0, 0);
            sv->setSpacing(4);
            auto *h1 = new QLabel(tr7("<b>View and create firewall rules</b>"));
            sv->addWidget(h1);
            auto *t1 = new QLabel(tr7("Create firewall rules to allow or block connections to specified ports or services."));
            t1->setWordWrap(true);
            sv->addWidget(t1);
            sv->addWidget(new Link(tr7("Inbound Rules"), [this] {
                select(InboundNode);
            }));
            sv->addWidget(new Link(tr7("Outbound Rules"), [this] {
                select(OutboundNode);
            }));
            sv->addSpacing(8);
            sv->addWidget(new QLabel(tr7("<b>View current firewall and IPsec policy and activity</b>")));
            auto *t2 = new QLabel(tr7("View information about currently applied firewall rules."));
            t2->setWordWrap(true);
            sv->addWidget(t2);
            sv->addWidget(new Link(tr7("Monitoring"), [this] {
                select(MonitoringNode);
            }));
            v->addWidget(new Section(tr7("Getting Started"), start));
        } else {
            auto *act = new QWidget;
            auto *av = new QVBoxLayout(act);
            av->setContentsMargins(0, 0, 0, 0);
            av->addLayout(line(QStringLiteral("seven-wf-firewallrulesfolder"), tr7("Active firewall rules: %1").arg(m_ufw.rules.size())));
            av->addWidget(new Link(tr7("View active firewall rules"), [this] {
                select(ActiveNode);
            }));
            v->addWidget(new Section(tr7("Active Rules"), act));
        }
        v->addStretch(1);
    }

    QWidget *buildRulesPage()
    {
        QWidget *page = pageWithHeader(m_rulesHeader);
        m_list = new QTreeWidget;
        m_list->setHeader(new VistaHeader(m_list));
        m_list->setHeaderLabels({tr7("Name"), tr7("Group"), tr7("Profile"), tr7("Enabled"), tr7("Action"),
                                 tr7("Application"), tr7("Local Address"), tr7("Remote Address"), tr7("Protocol"),
                                 tr7("Local Port"), tr7("Remote Port")});
        const QList<int> widths{230, 90, 70, 75, 150, 80, 110, 110, 70, 100, 100};
        for (int i = 0; i < widths.size(); ++i) {
            m_list->setColumnWidth(i, widths.at(i));
        }
        m_list->setRootIsDecorated(false);
        m_list->setUniformRowHeights(true);
        m_list->setFrameShape(QFrame::NoFrame);
        m_list->setSortingEnabled(true);
        m_list->setAllColumnsShowFocus(true);
        m_list->setContextMenuPolicy(Qt::CustomContextMenu);
        m_list->setStyleSheet(QStringLiteral("QTreeView { background: white; outline: 0; } QTreeView::item { height: 18px; }"));
        page->layout()->addWidget(m_list);
        connect(m_list, &QTreeWidget::currentItemChanged, this, [this] {
            refreshActions();
        });
        connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this] {
            ruleProperties();
        });
        connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
            if (!m_list->itemAt(pos)) {
                return;
            }
            QMenu m;
            for (const ActionItem &a : ruleActions()) {
                if (a.separatorBefore) {
                    m.addSeparator();
                }
                QAction *x = m.addAction(QIcon::fromTheme(a.icon), a.text);
                x->setEnabled(a.enabled);
                const auto run = a.run;
                connect(x, &QAction::triggered, this, [run] {
                    if (run) {
                        run();
                    }
                });
            }
            m.exec(m_list->viewport()->mapToGlobal(pos));
        });
        return page;
    }

    int node() const
    {
        return m_tree->currentItem() ? m_tree->currentItem()->data(0, Qt::UserRole).toInt() : RootNode;
    }

    void select(int n)
    {
        QTreeWidgetItemIterator it(m_tree);
        for (; *it; ++it) {
            if ((*it)->data(0, Qt::UserRole).toInt() == n) {
                if ((*it)->parent()) {
                    (*it)->parent()->setExpanded(true);
                }
                m_tree->setCurrentItem(*it);
                return;
            }
        }
    }

    void navigate(int dir)
    {
        QList<QTreeWidgetItem *> &from = dir < 0 ? m_history : m_future;
        QList<QTreeWidgetItem *> &to = dir < 0 ? m_future : m_history;
        if (from.isEmpty()) {
            return;
        }
        to.append(m_tree->currentItem());
        m_navigating = true;
        m_tree->setCurrentItem(from.takeLast());
        m_navigating = false;
        m_back->setEnabled(!m_history.isEmpty());
        m_forward->setEnabled(!m_future.isEmpty());
    }

    void reload()
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        m_ufw = Win7Firewall::queryUfw();
        QApplication::restoreOverrideCursor();
        showNode();
    }

    void showNode()
    {
        const int n = node();
        if (n == RootNode || n == MonitoringNode) {
            m_overviewHeader->setText(n == RootNode ? tr7("Windows Firewall with Advanced Security on Local Computer")
                                                    : tr7("Monitoring"));
            fillOverview(n == MonitoringNode);
            m_middle->setCurrentIndex(0);
        } else {
            fillRules(n);
            m_middle->setCurrentIndex(1);
        }
        refreshActions();
    }

    void fillRules(int n)
    {
        const QString dir = n == InboundNode ? QStringLiteral("in") : n == OutboundNode ? QStringLiteral("out") : QString();
        m_rulesHeader->setText(n == InboundNode ? tr7("Inbound Rules")
                                                : n == OutboundNode ? tr7("Outbound Rules") : tr7("Windows Firewall"));
        m_list->setSortingEnabled(false);
        m_list->clear();
        QSet<QString> seen; // ufw keeps an IPv4 and an IPv6 copy of most rules: Windows shows one rule
        for (const Rule &r : std::as_const(m_ufw.rules)) {
            if (!dir.isEmpty() && r.direction != dir) {
                continue;
            }
            const QString key = ruleKey(r) + r.action;
            if (seen.contains(key)) {
                continue;
            }
            seen.insert(key);
            const bool in = r.direction == QLatin1String("in");
            auto *it = new QTreeWidgetItem(m_list);
            it->setText(0, ruleName(r));
            it->setIcon(0, QIcon::fromTheme(r.action == QLatin1String("allow") || r.action == QLatin1String("limit") ? QStringLiteral("seven-wf-allow") : QStringLiteral("seven-wf-block")));
            it->setText(1, QString());
            it->setText(2, tr7("All"));
            it->setText(3, tr7("Yes"));
            it->setText(4, actionName(r.action));
            it->setText(5, tr7("Any", "address"));
            it->setText(6, any(in ? r.dst : r.src));
            it->setText(7, any(in ? r.src : r.dst));
            it->setText(8, r.dapp.isEmpty() ? any(r.protocol, tr7("Any")).toUpper().replace(tr7("ANY"), tr7("Any"))
                                            : tr7("Profile"));
            it->setText(9, any(in ? r.dport : r.sport, tr7("Any")));
            it->setText(10, any(in ? r.sport : r.dport, tr7("Any")));
            if (!r.dapp.isEmpty()) {
                it->setText(9, r.dapp);
            }
            if (n == ActiveNode) {
                it->setText(1, in ? tr7("Inbound") : tr7("Outbound"));
            }
            it->setData(0, Qt::UserRole, m_ufw.rules.indexOf(r));
        }
        m_list->setSortingEnabled(true);
        m_list->sortByColumn(0, Qt::AscendingOrder);
        statusBar()->showMessage(m_ufw.ok ? QString() : tr7("The firewall rules could not be read."));
    }

    Rule currentRule() const
    {
        QTreeWidgetItem *it = m_list->currentItem();
        return it ? m_ufw.rules.value(it->data(0, Qt::UserRole).toInt()) : Rule{};
    }

    QList<ActionItem> ruleActions()
    {
        const bool has = m_list->currentItem();
        return {
            {tr7("Delete"), QStringLiteral("seven-wf-delete"), [this] {
                 deleteRule();
             },
             has},
            {tr7("Properties"), QStringLiteral("seven-mmc-props"), [this] {
                 ruleProperties();
             },
             has, true},
            {tr7("Help"), QStringLiteral("seven-mmc-help"), [this] {
                 showHelp();
             },
             true, true},
        };
    }

    QList<ActionItem> nodeActions()
    {
        const int n = node();
        QList<ActionItem> items;
        if (n == InboundNode || n == OutboundNode) {
            items << ActionItem{tr7("New Rule..."), n == InboundNode ? QStringLiteral("seven-wf-newinbound") : QStringLiteral("seven-wf-newoutbound"),
                                [this, n] {
                                    newRule(n == InboundNode);
                                }}
                  << ActionItem{tr7("Export List..."), QStringLiteral("seven-wf-export"), [this] {
                                    exportList();
                                },
                                true, true};
        } else if (n == RootNode) {
            items << ActionItem{tr7("Restore Default Policy"), QString(), [this] {
                                    restoreDefaults();
                                }}
                  << ActionItem{tr7("Properties"), QStringLiteral("seven-mmc-props"), [this] {
                                    firewallProperties();
                                },
                                true, true};
        } else if (n == ActiveNode) {
            items << ActionItem{tr7("Export List..."), QStringLiteral("seven-wf-export"), [this] {
                exportList();
            }};
        }
        items << ActionItem{tr7("Refresh"), QStringLiteral("seven-mmc-refresh"), [this] {
                                reload();
                            },
                            true, true}
              << ActionItem{tr7("Help"), QStringLiteral("seven-mmc-help"), [this] {
                                showHelp();
                            },
                            true, true};
        return items;
    }

    void refreshActions()
    {
        QList<ActionGroup> groups{{m_tree->currentItem() ? m_tree->currentItem()->text(0) : QString(), nodeActions()}};
        if (m_middle->currentIndex() == 1 && m_list->currentItem()) {
            groups << ActionGroup{m_list->currentItem()->text(0), ruleActions()};
        }
        m_groups = groups;
        m_actions->setGroups(groups);
    }

    void fillMenu(QMenu *m, bool nodeOnly)
    {
        for (const ActionGroup &g : std::as_const(m_groups)) {
            for (const ActionItem &a : g.items) {
                if (a.separatorBefore) {
                    m->addSeparator();
                }
                QAction *x = m->addAction(QIcon::fromTheme(a.icon), a.text);
                x->setEnabled(a.enabled);
                const auto run = a.run;
                connect(x, &QAction::triggered, this, [run] {
                    if (run) {
                        run();
                    }
                });
            }
            if (nodeOnly) {
                break;
            }
            m->addSeparator();
        }
    }

    bool root(const QString &cmds)
    {
        const bool ok = Win7Firewall::runAsRoot(this, cmds);
        reload();
        return ok;
    }

    void newRule(bool inbound)
    {
        RuleWizard w(this, inbound);
        if (w.exec() != QDialog::Accepted || w.command().isEmpty()) {
            return;
        }
        const Ufw before = m_ufw;
        if (!root(w.command())) {
            return;
        }
        // remember the name of the new rule(s)
        KConfigGroup names = ruleNames();
        for (const Rule &r : std::as_const(m_ufw.rules)) {
            bool old = false;
            for (const Rule &b : before.rules) {
                old = old || (ruleKey(b) == ruleKey(r) && b.action == r.action);
            }
            if (!old) {
                names.writeEntry(ruleKey(r), w.ruleTitle());
            }
        }
        names.sync();
        showNode();
    }

    void deleteRule()
    {
        const Rule r = currentRule();
        if (QMessageBox::question(this, tr7("Delete Rule"), tr7("Are you sure you want to delete this rule?")) != QMessageBox::Yes) {
            return;
        }
        if (root(QStringLiteral("ufw delete ") + ruleSpec(r))) {
            KConfigGroup names = ruleNames();
            names.deleteEntry(ruleKey(r));
            names.sync();
        }
    }

    void ruleProperties()
    {
        const Rule r = currentRule();
        if (r.action.isEmpty()) {
            return;
        }
        QDialog dlg(this);
        dlg.setWindowTitle(tr7("%1 Properties").arg(ruleName(r)));
        dlg.setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-wf-rule")));
        dlg.setFixedSize(440, 480);
        auto *v = new QVBoxLayout(&dlg);
        auto *tabs = new QTabWidget;
        auto *general = new QWidget;
        auto *gv = new QVBoxLayout(general);
        auto *gGroup = new QGroupBox(tr7("General"));
        auto *gg = new QGridLayout(gGroup);
        auto *icon = new QLabel;
        icon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-wf-rule")).pixmap(32, 32));
        gg->addWidget(icon, 0, 0, 2, 1, Qt::AlignTop);
        gg->addWidget(new QLabel(tr7("Name:")), 0, 1);
        auto *name = new QLineEdit(ruleName(r));
        gg->addWidget(name, 1, 1);
        gv->addWidget(gGroup);
        auto *aGroup = new QGroupBox(tr7("Action"));
        auto *av = new QVBoxLayout(aGroup);
        auto *allow = new QRadioButton(tr7("Allow the connection"));
        auto *limit = new QRadioButton(tr7("Allow, but limit the connection rate"));
        auto *block = new QRadioButton(tr7("Block the connection"));
        (r.action == QLatin1String("allow") ? allow : r.action == QLatin1String("limit") ? limit : block)->setChecked(true);
        limit->setEnabled(r.direction == QLatin1String("in"));
        av->addWidget(allow);
        av->addWidget(limit);
        av->addWidget(block);
        gv->addWidget(aGroup);
        gv->addStretch(1);
        tabs->addTab(general, tr7("General"));
        auto *ports = new QWidget;
        auto *pg = new QGridLayout(ports);
        const bool in = r.direction == QLatin1String("in");
        const QList<QPair<QString, QString>> rows{{tr7("Protocol type:"), r.dapp.isEmpty() ? any(r.protocol, tr7("Any")).toUpper() : tr7("Profile ") + r.dapp},
                                                  {tr7("Local port:"), any(in ? r.dport : r.sport, tr7("All Ports"))},
                                                  {tr7("Remote port:"), any(in ? r.sport : r.dport, tr7("All Ports"))}};
        int row = 0;
        for (const auto &[k, val] : rows) {
            pg->addWidget(new QLabel(k), row, 0);
            pg->addWidget(new QLabel(val), row++, 1);
        }
        pg->setRowStretch(row, 1);
        pg->setColumnStretch(1, 1);
        tabs->addTab(ports, tr7("Protocols and Ports"));
        auto *scope = new QWidget;
        auto *sg = new QGridLayout(scope);
        sg->addWidget(new QLabel(tr7("Local IP address:")), 0, 0);
        sg->addWidget(new QLabel(any(in ? r.dst : r.src, tr7("Any IP address"))), 0, 1);
        sg->addWidget(new QLabel(tr7("Remote IP address:")), 1, 0);
        sg->addWidget(new QLabel(any(in ? r.src : r.dst, tr7("Any IP address"))), 1, 1);
        sg->setRowStretch(2, 1);
        sg->setColumnStretch(1, 1);
        tabs->addTab(scope, tr7("Scope"));
        v->addWidget(tabs, 1);
        auto *btns = new QHBoxLayout;
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        for (QPushButton *b : {ok, cancel}) {
            b->setMinimumWidth(80);
            btns->addWidget(b);
        }
        v->addLayout(btns);
        connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        KConfigGroup names = ruleNames();
        names.writeEntry(ruleKey(r), name->text().trimmed());
        names.sync();
        const QString action = allow->isChecked() ? QStringLiteral("allow") : limit->isChecked() ? QStringLiteral("limit") : QStringLiteral("deny");
        if (action != r.action) {
            // the same rule with the other action, in the same place of the list
            root(QStringLiteral("ufw delete %1 && ufw insert %2 %3").arg(ruleSpec(r)).arg(qMax(1, r.position)).arg(ruleSpec(r, action)));
        } else {
            showNode();
        }
    }

    void firewallProperties()
    {
        QDialog dlg(this);
        dlg.setWindowTitle(tr7("Windows Firewall with Advanced Security on Local Computer Properties"));
        dlg.setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-wf-firewall")));
        dlg.setFixedSize(460, 420);
        auto *v = new QVBoxLayout(&dlg);
        auto *tabs = new QTabWidget;
        auto *page = new QWidget;
        auto *pv = new QVBoxLayout(page);
        auto *intro = new QLabel(tr7("Specify behavior for all networks the computer is connected to."));
        intro->setWordWrap(true);
        pv->addWidget(intro);
        auto *state = new QGroupBox(tr7("Status"));
        auto *sg = new QGridLayout(state);
        auto *icon = new QLabel;
        icon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-wf-shield")).pixmap(32, 32));
        sg->addWidget(icon, 0, 0, 3, 1, Qt::AlignTop);
        auto *on = new QComboBox;
        on->addItems({tr7("On (recommended)"), tr7("Off", "state")});
        on->setCurrentIndex(m_ufw.enabled ? 0 : 1);
        auto *inbound = new QComboBox;
        inbound->addItem(tr7("Block (default)"), QStringLiteral("deny"));
        inbound->addItem(tr7("Reject with a message"), QStringLiteral("reject"));
        inbound->addItem(tr7("Allow"), QStringLiteral("allow"));
        inbound->setCurrentIndex(qMax(0, inbound->findData(m_ufw.incoming)));
        auto *outbound = new QComboBox;
        outbound->addItem(tr7("Allow (default)"), QStringLiteral("allow"));
        outbound->addItem(tr7("Block"), QStringLiteral("deny"));
        outbound->setCurrentIndex(qMax(0, outbound->findData(m_ufw.outgoing)));
        sg->addWidget(new QLabel(tr7("Windows Firewall state:")), 0, 1);
        sg->addWidget(on, 0, 2);
        sg->addWidget(new QLabel(tr7("Incoming connections:")), 1, 1);
        sg->addWidget(inbound, 1, 2);
        sg->addWidget(new QLabel(tr7("Outbound connections:")), 2, 1);
        sg->addWidget(outbound, 2, 2);
        pv->addWidget(state);
        pv->addStretch(1);
        tabs->addTab(page, tr7("Public Profile"));
        v->addWidget(tabs, 1);
        auto *btns = new QHBoxLayout;
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        for (QPushButton *b : {ok, cancel}) {
            b->setMinimumWidth(80);
            btns->addWidget(b);
        }
        v->addLayout(btns);
        connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        QStringList cmds;
        if (inbound->currentData().toString() != m_ufw.incoming) {
            cmds << QStringLiteral("ufw default %1 incoming").arg(inbound->currentData().toString());
        }
        if (outbound->currentData().toString() != m_ufw.outgoing) {
            cmds << QStringLiteral("ufw default %1 outgoing").arg(outbound->currentData().toString());
        }
        if ((on->currentIndex() == 0) != m_ufw.enabled) {
            cmds << (on->currentIndex() == 0 ? QStringLiteral("ufw --force enable") : QStringLiteral("ufw disable"));
        }
        if (!cmds.isEmpty()) {
            root(cmds.join(QStringLiteral(" && ")));
        }
    }

    void restoreDefaults()
    {
        if (QMessageBox::warning(this, tr7("Restore Default Policy", "title"),
                                 tr7("All firewall rules will be removed; inbound connections will be blocked and outbound ones allowed.\n\nDo you want to continue?"),
                                 QMessageBox::Yes | QMessageBox::No)
            != QMessageBox::Yes) {
            return;
        }
        if (root(QStringLiteral("ufw --force reset && ufw default deny incoming && ufw default allow outgoing && ufw --force enable"))) {
            KConfigGroup names = ruleNames();
            names.deleteGroup();
            names.sync();
        }
    }

    void exportList()
    {
        const QString f = QFileDialog::getSaveFileName(this, tr7("Export List", "title"), QDir::homePath() + tr7("/Firewall rules.txt"),
                                                       tr7("Text (Tab delimited) (*.txt);;CSV (Comma Separated) (*.csv)"));
        if (f.isEmpty()) {
            return;
        }
        QFile file(f);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return;
        }
        const QString sep = f.endsWith(QLatin1String(".csv")) ? QStringLiteral(",") : QStringLiteral("\t");
        QTextStream out(&file);
        QStringList head;
        for (int c = 0; c < m_list->columnCount(); ++c) {
            head << m_list->headerItem()->text(c);
        }
        out << head.join(sep) << "\n";
        for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
            QStringList row;
            for (int c = 0; c < m_list->columnCount(); ++c) {
                row << m_list->topLevelItem(i)->text(c);
            }
            out << row.join(sep) << "\n";
        }
    }

    void showHelp()
    {
        QMessageBox::information(this, windowTitle(),
                                 tr7("Here you can view and change the firewall rules (ufw).\n\n• Inbound Rules - what can be opened on this computer from the network.\n• Outbound Rules - where programs can connect.\n• The Linux firewall filters ports, not individual programs, so rules are created for ports and services."));
    }

    QTreeWidget *m_tree;
    QStackedWidget *m_middle;
    ActionPane *m_actions;
    DarkHeader *m_overviewHeader;
    QWidget *m_overview;
    DarkHeader *m_rulesHeader;
    QTreeWidget *m_list;
    QAction *m_back;
    QAction *m_forward;
    QMenu *m_actionMenu;
    QList<ActionGroup> m_groups;
    QList<QTreeWidgetItem *> m_history;
    QList<QTreeWidgetItem *> m_future;
    bool m_navigating = false;
    Ufw m_ufw;
};
} // namespace Win7Wf
