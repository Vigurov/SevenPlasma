/*
 * Internet Options (inetcpl.cpl), the tabs Linux can back: General (the home page, given to Google Chrome as a
 * recommended policy), Connections (the LAN settings: KDE's proxy in kioslaverc, which Chrome and KIO read, and
 * GNOME's for GTK programs) and Programs (the default web browser and e-mail program, through xdg-settings).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "win7folderopts.h"

#include <KApplicationTrader>
#include <KConfigGroup>
#include <KService>
#include <KSharedConfig>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDialog>
#include <QFile>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTabWidget>

namespace Win7Inet
{

using Win7FolderOpts::dlu;

template<typename T>
T *place(QWidget *parent, T *w, const QRect &r)
{
    w->setParent(parent);
    w->setGeometry(r);
    return w;
}

inline QString run(const QString &exe, const QStringList &args)
{
    QProcess p;
    p.start(exe, args);
    p.waitForFinished(5000);
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

/* ---------------- proxy ---------------- */

struct Proxy {
    int type = 0; // KDE: 0 none, 1 manual, 2 script, 3 detect automatically
    QString script;
    QString http, https, ftp, socks; // "host:port"
    bool bypassLocal = true;
};

/** "http://host 8080" / "http://host:8080" -> "host:8080" */
inline QString hostPort(QString v)
{
    v = v.trimmed();
    v.remove(QRegularExpression(QStringLiteral("^[a-z]+://")));
    v.replace(QLatin1Char(' '), QLatin1Char(':'));
    return v == QLatin1String(":") ? QString() : v;
}

inline Proxy readProxy()
{
    const KConfigGroup g(KSharedConfig::openConfig(QStringLiteral("kioslaverc")), QStringLiteral("Proxy Settings"));
    Proxy p;
    p.type = g.readEntry("ProxyType", 0);
    p.script = g.readEntry("Proxy Config Script", QString());
    p.http = hostPort(g.readEntry("httpProxy", QString()));
    p.https = hostPort(g.readEntry("httpsProxy", QString()));
    p.ftp = hostPort(g.readEntry("ftpProxy", QString()));
    p.socks = hostPort(g.readEntry("socksProxy", QString()));
    p.bypassLocal = g.readEntry("NoProxyFor", QString()).contains(QLatin1String("localhost"));
    return p;
}

inline void writeProxy(const Proxy &p)
{
    KConfigGroup g(KSharedConfig::openConfig(QStringLiteral("kioslaverc")), QStringLiteral("Proxy Settings"));
    auto url = [](const QString &scheme, const QString &hp) {
        if (hp.isEmpty()) {
            return QString();
        }
        const int colon = hp.lastIndexOf(QLatin1Char(':'));
        return colon > 0 ? QStringLiteral("%1://%2 %3").arg(scheme, hp.left(colon), hp.mid(colon + 1)) : QStringLiteral("%1://%2").arg(scheme, hp);
    };
    g.writeEntry("ProxyType", p.type);
    g.writeEntry("Proxy Config Script", p.script);
    g.writeEntry("httpProxy", url(QStringLiteral("http"), p.http));
    g.writeEntry("httpsProxy", url(QStringLiteral("http"), p.https));
    g.writeEntry("ftpProxy", url(QStringLiteral("http"), p.ftp));
    g.writeEntry("socksProxy", url(QStringLiteral("socks"), p.socks));
    g.writeEntry("NoProxyFor", p.bypassLocal ? QStringLiteral("localhost,127.0.0.1,::1,.local") : QString());
    g.writeEntry("ReversedException", false);
    g.sync();
    // KIO picks the change up at once, as the proxy page of System Settings tells it
    QDBusMessage msg = QDBusMessage::createSignal(QStringLiteral("/KIO/Scheduler"), QStringLiteral("org.kde.KIO.Scheduler"), QStringLiteral("reparseSlaveConfiguration"));
    msg << QString();
    QDBusConnection::sessionBus().send(msg);

    // GTK programs read GNOME's settings
    static const char *modes[] = {"none", "manual", "auto", "auto"};
    QProcess::execute(QStringLiteral("gsettings"), {QStringLiteral("set"), QStringLiteral("org.gnome.system.proxy"), QStringLiteral("mode"), QLatin1String(modes[qBound(0, p.type, 3)])});
    QProcess::execute(QStringLiteral("gsettings"), {QStringLiteral("set"), QStringLiteral("org.gnome.system.proxy"), QStringLiteral("autoconfig-url"), p.type == 2 ? p.script : QString()});
    const QList<QPair<QString, QString>> schemes{{QStringLiteral("http"), p.http}, {QStringLiteral("https"), p.https}, {QStringLiteral("ftp"), p.ftp}, {QStringLiteral("socks"), p.socks}};
    for (const auto &s : schemes) {
        const int colon = s.second.lastIndexOf(QLatin1Char(':'));
        const QString host = colon > 0 ? s.second.left(colon) : s.second;
        const QString port = colon > 0 ? s.second.mid(colon + 1) : QStringLiteral("0");
        const QString schema = QStringLiteral("org.gnome.system.proxy.") + s.first;
        QProcess::execute(QStringLiteral("gsettings"), {QStringLiteral("set"), schema, QStringLiteral("host"), host});
        QProcess::execute(QStringLiteral("gsettings"), {QStringLiteral("set"), schema, QStringLiteral("port"), port});
    }
}

/** "LAN Settings" (inetcpl dialog): automatic configuration and the proxy server */
class LanDialog : public QDialog
{
public:
    explicit LanDialog(QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("Настроювання параметрів локальної мережі"));
        setFixedSize(dlu(0, 0, 262, 192).size());
        m_p = readProxy();
        place(this, new QGroupBox(QStringLiteral("Автоматичне настроювання")), dlu(7, 7, 248, 74));
        place(this, new QLabel(QStringLiteral("Автоматичне настроювання може замінити параметри, установлені вручну. Щоб використовувати параметри, установлені вручну, вимкніть автоматичне настроювання.")),
              dlu(14, 17, 234, 26))
            ->setWordWrap(true);
        m_detect = place(this, new QCheckBox(QStringLiteral("Автоматичне визначення параметрів")), dlu(14, 44, 230, 10));
        m_useScript = place(this, new QCheckBox(QStringLiteral("Використовувати сценарій автоматичного настроювання")), dlu(14, 56, 230, 10));
        place(this, new QLabel(QStringLiteral("Адреса")), dlu(26, 68, 30, 8));
        m_script = place(this, new QLineEdit(m_p.script), dlu(58, 66, 190, 12));
        place(this, new QGroupBox(QStringLiteral("Проксі-сервер")), dlu(7, 86, 248, 78));
        m_useProxy = place(this, new QCheckBox(QStringLiteral("Використовувати проксі-сервер для локальної мережі (ці параметри не застосовуються до віддалених або VPN-підключень).")),
                           dlu(14, 96, 234, 18));
        place(this, new QLabel(QStringLiteral("Адреса:")), dlu(26, 120, 28, 8));
        m_host = place(this, new QLineEdit, dlu(56, 118, 92, 12));
        place(this, new QLabel(QStringLiteral("Порт:")), dlu(154, 120, 20, 8));
        m_port = place(this, new QLineEdit, dlu(176, 118, 28, 12));
        m_advanced = place(this, new QPushButton(QStringLiteral("Додатково")), dlu(208, 117, 42, 14));
        m_bypass = place(this, new QCheckBox(QStringLiteral("Не використовувати проксі-сервер для локальних адрес")), dlu(26, 140, 224, 10));
        auto *ok = place(this, new QPushButton(QStringLiteral("OK")), dlu(151, 172, 50, 14));
        auto *cancel = place(this, new QPushButton(QStringLiteral("Скасувати")), dlu(205, 172, 50, 14));
        ok->setDefault(true);
        m_detect->setChecked(m_p.type == 3);
        m_useScript->setChecked(m_p.type == 2);
        m_useProxy->setChecked(m_p.type == 1);
        const int colon = m_p.http.lastIndexOf(QLatin1Char(':'));
        m_host->setText(colon > 0 ? m_p.http.left(colon) : m_p.http);
        m_port->setText(colon > 0 ? m_p.http.mid(colon + 1) : QString());
        m_bypass->setChecked(m_p.bypassLocal);
        // one kind at a time, as KDE keeps one proxy type
        connect(m_detect, &QCheckBox::toggled, this, [this](bool on) {
            if (on) {
                m_useScript->setChecked(false);
                m_useProxy->setChecked(false);
            }
            sync();
        });
        connect(m_useScript, &QCheckBox::toggled, this, [this](bool on) {
            if (on) {
                m_detect->setChecked(false);
                m_useProxy->setChecked(false);
            }
            sync();
        });
        connect(m_useProxy, &QCheckBox::toggled, this, [this](bool on) {
            if (on) {
                m_detect->setChecked(false);
                m_useScript->setChecked(false);
            }
            sync();
        });
        connect(m_advanced, &QPushButton::clicked, this, [this] { advanced(); });
        connect(ok, &QPushButton::clicked, this, [this] {
            m_p.type = m_useProxy->isChecked() ? 1 : (m_useScript->isChecked() ? 2 : (m_detect->isChecked() ? 3 : 0));
            m_p.script = m_script->text().trimmed();
            const QString hp = m_host->text().trimmed().isEmpty() ? QString() : m_host->text().trimmed() + (m_port->text().trimmed().isEmpty() ? QString() : QLatin1Char(':') + m_port->text().trimmed());
            if (m_sameForAll || m_p.http.isEmpty()) {
                m_p.https = m_p.ftp = hp;
            }
            m_p.http = hp;
            m_p.bypassLocal = m_bypass->isChecked();
            writeProxy(m_p);
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        m_sameForAll = m_p.https.isEmpty() || m_p.https == m_p.http;
        sync();
    }

private:
    void sync()
    {
        m_script->setEnabled(m_useScript->isChecked());
        for (QWidget *w : std::initializer_list<QWidget *>{m_host, m_port, m_advanced, m_bypass}) {
            w->setEnabled(m_useProxy->isChecked());
        }
    }

    /** "Proxy Settings": a server for each protocol */
    void advanced()
    {
        QDialog d(this);
        d.setWindowTitle(QStringLiteral("Параметри проксі-сервера"));
        auto *v = new QVBoxLayout(&d);
        auto *g = new QGroupBox(QStringLiteral("Сервери"));
        auto *grid = new QGridLayout(g);
        grid->addWidget(new QLabel(QStringLiteral("Тип")), 0, 0);
        grid->addWidget(new QLabel(QStringLiteral("Адреса проксі-сервера")), 0, 1);
        grid->addWidget(new QLabel(QStringLiteral("Порт")), 0, 2);
        const QString http = m_host->text().trimmed() + (m_port->text().isEmpty() ? QString() : QLatin1Char(':') + m_port->text().trimmed());
        QList<QPair<QLineEdit *, QLineEdit *>> rows;
        const QStringList names{QStringLiteral("HTTP:"), QStringLiteral("Безпечний:"), QStringLiteral("FTP:"), QStringLiteral("Socks:")};
        const QStringList values{http, m_p.https, m_p.ftp, m_p.socks};
        for (int i = 0; i < 4; ++i) {
            grid->addWidget(new QLabel(names.at(i)), i + 1, 0);
            const int colon = values.at(i).lastIndexOf(QLatin1Char(':'));
            auto *h = new QLineEdit(colon > 0 ? values.at(i).left(colon) : values.at(i));
            auto *pt = new QLineEdit(colon > 0 ? values.at(i).mid(colon + 1) : QString());
            pt->setMaximumWidth(60);
            grid->addWidget(h, i + 1, 1);
            grid->addWidget(pt, i + 1, 2);
            rows.append(qMakePair(h, pt));
        }
        auto *same = new QCheckBox(QStringLiteral("Використовувати один проксі-сервер для всіх протоколів"));
        same->setChecked(m_sameForAll);
        grid->addWidget(same, 5, 1, 1, 2);
        v->addWidget(g);
        auto *bar = new QHBoxLayout;
        bar->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
        bar->addWidget(ok);
        bar->addWidget(cancel);
        v->addLayout(bar);
        connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
        if (d.exec() != QDialog::Accepted) {
            return;
        }
        auto join = [](const QPair<QLineEdit *, QLineEdit *> &r) {
            const QString h = r.first->text().trimmed();
            return h.isEmpty() ? QString() : h + (r.second->text().trimmed().isEmpty() ? QString() : QLatin1Char(':') + r.second->text().trimmed());
        };
        m_sameForAll = same->isChecked();
        m_host->setText(rows[0].first->text().trimmed());
        m_port->setText(rows[0].second->text().trimmed());
        m_p.https = m_sameForAll ? join(rows[0]) : join(rows[1]);
        m_p.ftp = m_sameForAll ? join(rows[0]) : join(rows[2]);
        m_p.socks = m_sameForAll ? QString() : join(rows[3]);
    }

    Proxy m_p;
    bool m_sameForAll = true;
    QCheckBox *m_detect, *m_useScript, *m_useProxy, *m_bypass;
    QLineEdit *m_script, *m_host, *m_port;
    QPushButton *m_advanced;
};

/* ---------------- default programs ---------------- */

struct App {
    QString desktop;
    QString name;
    QString icon;
};

inline QList<App> handlers(const QString &mime)
{
    QList<App> list;
    const KService::List services = KApplicationTrader::queryByMimeType(mime);
    for (const KService::Ptr &s : services) {
        list.append(App{s->desktopEntryName() + QStringLiteral(".desktop"), s->name(), s->icon()});
    }
    return list;
}

/* ---------------- home page (Google Chrome's recommended policy) ---------------- */

inline QString chromePolicyFile()
{
    return QStringLiteral("/etc/opt/chrome/policies/recommended/seven-internet-options.json");
}

inline QStringList homePages()
{
    QFile f(chromePolicyFile());
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    QStringList pages;
    for (const QJsonValue &v : o.value(QStringLiteral("RestoreOnStartupURLs")).toArray()) {
        pages << v.toString();
    }
    return pages;
}

inline bool setHomePages(QWidget *parent, const QStringList &pages)
{
    QJsonObject o;
    if (!pages.isEmpty() && pages.first() != QLatin1String("about:blank")) {
        o.insert(QStringLiteral("HomepageLocation"), pages.first());
        o.insert(QStringLiteral("HomepageIsNewTabPage"), false);
        o.insert(QStringLiteral("ShowHomeButton"), true);
        o.insert(QStringLiteral("RestoreOnStartup"), 4);
        o.insert(QStringLiteral("RestoreOnStartupURLs"), QJsonArray::fromStringList(pages));
    } else {
        o.insert(QStringLiteral("HomepageIsNewTabPage"), true);
        o.insert(QStringLiteral("RestoreOnStartup"), 5);
    }
    const QByteArray json = QJsonDocument(o).toJson();
    QProcess p;
    p.start(QStringLiteral("pkexec"), {QStringLiteral("sh"), QStringLiteral("-c"),
                                       QStringLiteral("mkdir -p /etc/opt/chrome/policies/recommended && cat > %1").arg(chromePolicyFile())});
    p.waitForStarted();
    p.write(json);
    p.closeWriteChannel();
    p.waitForFinished(-1);
    if (p.exitCode() != 0) {
        if (p.exitCode() != 126 && p.exitCode() != 127) {
            QMessageBox::warning(parent, QStringLiteral("Властивості оглядача"), QStringLiteral("Не вдалося зберегти домашню сторінку."));
        }
        return false;
    }
    return true;
}

/* ---------------- the dialog ---------------- */

class InternetOptions : public QDialog
{
public:
    InternetOptions()
    {
        setWindowTitle(QStringLiteral("Властивості оглядача"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-cpi-inetopts")));
        setAttribute(Qt::WA_DeleteOnClose);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 7, 7, 7);
        auto *tabs = new QTabWidget;
        tabs->addTab(generalPage(), QStringLiteral("Загальні"));
        tabs->addTab(connectionsPage(), QStringLiteral("Підключення"));
        tabs->addTab(programsPage(), QStringLiteral("Програми"));
        v->addWidget(tabs);
        auto *bar = new QHBoxLayout;
        bar->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
        m_apply = new QPushButton(QStringLiteral("Застосувати"));
        for (QPushButton *b : {ok, cancel, m_apply}) {
            b->setMinimumWidth(75);
            bar->addWidget(b);
        }
        ok->setDefault(true);
        m_apply->setEnabled(false);
        v->addLayout(bar);
        connect(ok, &QPushButton::clicked, this, [this] {
            if (apply()) {
                accept();
            }
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_apply, &QPushButton::clicked, this, [this] {
            if (apply()) {
                m_apply->setEnabled(false);
            }
        });
        setFixedSize(sizeHint());
    }

private:
    QWidget *page()
    {
        auto *p = new QWidget;
        p->setFixedSize(dlu(0, 0, 252, 218).size());
        return p;
    }

    QWidget *generalPage()
    {
        QWidget *p = page();
        place(p, new QGroupBox(QStringLiteral("Домашня сторінка")), dlu(7, 7, 238, 84));
        auto *ic = place(p, new QLabel, dlu(14, 20, 21, 20));
        ic->setPixmap(QIcon::fromTheme(QStringLiteral("go-home")).pixmap(32, 32));
        place(p, new QLabel(QStringLiteral("Щоб створити вкладки домашньої сторінки, введіть кожну адресу в окремому рядку.")), dlu(42, 18, 196, 18))->setWordWrap(true);
        m_home = place(p, new QPlainTextEdit(homePages().join(QLatin1Char('\n'))), dlu(42, 38, 196, 28));
        connect(m_home, &QPlainTextEdit::textChanged, this, [this] {
            m_homeChanged = true;
            m_apply->setEnabled(true);
        });
        auto *def = place(p, new QPushButton(QStringLiteral("За замовчуванням")), dlu(104, 70, 68, 14));
        auto *blank = place(p, new QPushButton(QStringLiteral("Пуста")), dlu(176, 70, 62, 14));
        connect(def, &QPushButton::clicked, this, [this] { m_home->setPlainText(QStringLiteral("https://www.google.com/")); });
        connect(blank, &QPushButton::clicked, this, [this] { m_home->setPlainText(QStringLiteral("about:blank")); });
        auto *note = place(p, new QLabel(QStringLiteral("Домашня сторінка застосовується в Google Chrome.")), dlu(14, 98, 230, 10));
        note->setStyleSheet(QStringLiteral("color: #5a6779;"));
        return p;
    }

    QWidget *connectionsPage()
    {
        QWidget *p = page();
        auto *ic = place(p, new QLabel, dlu(7, 9, 21, 20));
        ic->setPixmap(QIcon::fromTheme(QStringLiteral("seven-net-center"), QIcon::fromTheme(QStringLiteral("network-wired"))).pixmap(32, 32));
        place(p, new QLabel(QStringLiteral("Щоб установити підключення до Інтернету, натисніть кнопку «Установити».")), dlu(36, 10, 140, 20))->setWordWrap(true);
        auto *setup = place(p, new QPushButton(QStringLiteral("Установити")), dlu(185, 10, 60, 14));
        connect(setup, &QPushButton::clicked, this, [] { QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--net-connect")}); });
        place(p, new QGroupBox(QStringLiteral("Параметри локальної мережі (LAN)")), dlu(7, 150, 238, 42));
        place(p, new QLabel(QStringLiteral("Параметри локальної мережі не застосовуються до віддалених підключень.")), dlu(14, 162, 146, 20))->setWordWrap(true);
        auto *lan = place(p, new QPushButton(QStringLiteral("Параметри мережі")), dlu(164, 166, 76, 14));
        connect(lan, &QPushButton::clicked, this, [this] {
            LanDialog d(this);
            d.exec();
        });
        return p;
    }

    QWidget *programsPage()
    {
        QWidget *p = page();
        place(p, new QGroupBox(QStringLiteral("Веб-браузер за замовчуванням")), dlu(7, 7, 238, 58));
        m_browser = place(p, new QComboBox, dlu(14, 22, 130, 14));
        const QString current = run(QStringLiteral("xdg-settings"), {QStringLiteral("get"), QStringLiteral("default-web-browser")});
        for (const App &a : handlers(QStringLiteral("x-scheme-handler/http"))) {
            m_browser->addItem(QIcon::fromTheme(a.icon), a.name, a.desktop);
        }
        m_browser->setCurrentIndex(qMax(0, m_browser->findData(current)));
        auto *makeDefault = place(p, new QPushButton(QStringLiteral("Зробити за замовчуванням")), dlu(148, 21, 92, 14));
        m_browserState = place(p, new QLabel, dlu(14, 42, 226, 18));
        m_browserState->setWordWrap(true);
        auto showState = [this] {
            const QString cur = run(QStringLiteral("xdg-settings"), {QStringLiteral("get"), QStringLiteral("default-web-browser")});
            const bool isDefault = cur == m_browser->currentData().toString();
            m_browserState->setText(isDefault ? QStringLiteral("%1 — веб-браузер за замовчуванням.").arg(m_browser->currentText())
                                              : QStringLiteral("%1 не є веб-браузером за замовчуванням.").arg(m_browser->currentText()));
        };
        showState();
        connect(m_browser, &QComboBox::currentIndexChanged, this, showState);
        connect(makeDefault, &QPushButton::clicked, this, [this, showState] {
            QProcess::execute(QStringLiteral("xdg-settings"), {QStringLiteral("set"), QStringLiteral("default-web-browser"), m_browser->currentData().toString()});
            showState();
        });

        place(p, new QGroupBox(QStringLiteral("Програми Інтернету")), dlu(7, 72, 238, 64));
        place(p, new QLabel(QStringLiteral("Виберіть програми, які використовуватимуться для інших служб Інтернету, наприклад для електронної пошти.")), dlu(14, 84, 134, 44))
            ->setWordWrap(true);
        auto *programs = place(p, new QPushButton(QStringLiteral("Установити програми")), dlu(152, 86, 88, 14));
        connect(programs, &QPushButton::clicked, this, [] { QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--default-programs")}); });
        place(p, new QLabel(QStringLiteral("Електронна пошта:")), dlu(14, 146, 60, 10));
        m_mail = place(p, new QComboBox, dlu(80, 144, 160, 14));
        const QString mailNow = run(QStringLiteral("xdg-mime"), {QStringLiteral("query"), QStringLiteral("default"), QStringLiteral("x-scheme-handler/mailto")});
        for (const App &a : handlers(QStringLiteral("x-scheme-handler/mailto"))) {
            m_mail->addItem(QIcon::fromTheme(a.icon), a.name, a.desktop);
        }
        m_mail->setCurrentIndex(qMax(0, m_mail->findData(mailNow)));
        m_mailStart = m_mail->currentIndex();
        connect(m_mail, &QComboBox::currentIndexChanged, this, [this] { m_apply->setEnabled(true); });
        return p;
    }

    bool apply()
    {
        if (m_homeChanged) {
            QStringList pages;
            for (const QString &l : m_home->toPlainText().split(QLatin1Char('\n'))) {
                if (!l.trimmed().isEmpty()) {
                    pages << l.trimmed();
                }
            }
            if (!setHomePages(this, pages)) {
                return false;
            }
            m_homeChanged = false;
        }
        if (m_mail->currentIndex() != m_mailStart && m_mail->currentIndex() >= 0) {
            QProcess::execute(QStringLiteral("xdg-mime"), {QStringLiteral("default"), m_mail->currentData().toString(), QStringLiteral("x-scheme-handler/mailto")});
            m_mailStart = m_mail->currentIndex();
        }
        return true;
    }

    QPushButton *m_apply = nullptr;
    QPlainTextEdit *m_home = nullptr;
    bool m_homeChanged = false;
    QComboBox *m_browser = nullptr;
    QLabel *m_browserState = nullptr;
    QComboBox *m_mail = nullptr;
    int m_mailStart = -1;
};

} // namespace Win7Inet
