#pragma once
#include "win7i18n.h"
/*
 * Windows 7 "Advanced sharing settings" (Control Panel > Network and Sharing Center), after the real one:
 * the "Home or Work" and "Public" profiles, each with network discovery, file and printer sharing, Public
 * folder sharing, media streaming, file sharing connections and password protected sharing (+ HomeGroup
 * connections for Home or Work), the current profile marked and expanded, "Save changes" with the shield.
 *
 * Both profiles are kept in ~/.config/seven-netrc ([Sharing-home], [Sharing-public]; Work uses Home). The
 * current profile is applied to the system with one pkexec (the UAC prompt):
 *   network discovery      -> avahi-daemon on / off
 *   file and printer sharing -> smbd on / off (the samba package is offered when it is missing)
 *   Public folder sharing  -> [Public] share of ~/Public in /etc/samba/seven-shares.conf
 *   40- or 56-bit devices  -> server min protocol = NT1 in /etc/samba/seven-global.conf
 *   password protected     -> guest ok = no / yes on that share
 * HomeGroup connections has no Linux counterpart and is only remembered.
 */
#include "win7actioncenter.h"
#include "win7network.h"

#include <QButtonGroup>
#include <QEventLoop>
#include <QGuiApplication>

namespace Win7Sharing
{
struct Settings {
    bool discovery = true;
    bool files = false;
    bool publicFolder = false;
    bool strong = true;      // 128-bit encryption
    bool password = true;    // password protected sharing
    bool homegroupWindows = true;
    bool operator==(const Settings &) const = default;
};

/** "home" (Home or Work) or "public" */
inline QString profileOf(const QString &location)
{
    return location == QLatin1String("public") ? QStringLiteral("public") : QStringLiteral("home");
}

inline Settings defaults(const QString &profile)
{
    Settings s;
    s.discovery = profile != QLatin1String("public");
    return s;
}

inline Settings stored(const QString &profile)
{
    const Settings d = defaults(profile);
    QSettings &st = Win7Net::netStore();
    st.beginGroup(QStringLiteral("Sharing-") + profile);
    Settings s;
    s.discovery = st.value(QStringLiteral("Discovery"), d.discovery).toBool();
    s.files = st.value(QStringLiteral("Files"), d.files).toBool();
    s.publicFolder = st.value(QStringLiteral("PublicFolder"), d.publicFolder).toBool();
    s.strong = st.value(QStringLiteral("Strong"), d.strong).toBool();
    s.password = st.value(QStringLiteral("Password"), d.password).toBool();
    s.homegroupWindows = st.value(QStringLiteral("HomegroupWindows"), d.homegroupWindows).toBool();
    st.endGroup();
    return s;
}

inline void store(const QString &profile, const Settings &s)
{
    QSettings &st = Win7Net::netStore();
    st.beginGroup(QStringLiteral("Sharing-") + profile);
    st.setValue(QStringLiteral("Discovery"), s.discovery);
    st.setValue(QStringLiteral("Files"), s.files);
    st.setValue(QStringLiteral("PublicFolder"), s.publicFolder);
    st.setValue(QStringLiteral("Strong"), s.strong);
    st.setValue(QStringLiteral("Password"), s.password);
    st.setValue(QStringLiteral("HomegroupWindows"), s.homegroupWindows);
    st.endGroup();
    st.sync();
}

inline bool unitActive(const QString &unit)
{
    QProcess p;
    p.start(QStringLiteral("systemctl"), {QStringLiteral("is-active"), unit});
    p.waitForFinished(3000);
    return p.readAllStandardOutput().trimmed() == "active";
}

inline QString readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

/** What the system does now (the remembered values where the system has nothing to say) */
inline Settings systemState(const Settings &remembered)
{
    Settings s = remembered;
    s.discovery = unitActive(QStringLiteral("avahi-daemon"));
    s.files = unitActive(QStringLiteral("smbd")) || unitActive(QStringLiteral("smb"));
    const QString shares = readFile(QStringLiteral("/etc/samba/seven-shares.conf"));
    s.publicFolder = shares.contains(QLatin1String("[Public]"));
    s.strong = !readFile(QStringLiteral("/etc/samba/seven-global.conf")).contains(QLatin1String("NT1"));
    if (s.publicFolder) {
        s.password = shares.contains(QLatin1String("guest ok = no"));
    }
    return s;
}

/** The profile of the network in use, empty when there is none */
inline QString currentProfile()
{
    for (const QStringList &d : Win7Net::terse({QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("TYPE,STATE,CON-UUID"), QStringLiteral("dev")})) {
        if (d.value(1).startsWith(QLatin1String("connected")) && !d.value(1).contains(QLatin1String("externally"))
            && d.value(0) != QLatin1String("loopback") && d.value(0) != QLatin1String("bridge") && d.value(0) != QLatin1String("tun")) {
            return profileOf(Win7Net::networkLocation(d.value(2)));
        }
    }
    return {};
}

/** Puts the settings into effect (one pkexec); false when it was cancelled or failed */
inline bool apply(QWidget *parent, const Settings &want)
{
    const Settings now = systemState(want);
    const bool sambaNeeded = want.files || want.publicFolder;
    if (now.discovery == want.discovery && now.files == sambaNeeded && now.publicFolder == want.publicFolder && now.strong == want.strong
        && (!want.publicFolder || now.password == want.password)) {
        return true;
    }
    bool install = false;
    if (sambaNeeded && !QFile::exists(QStringLiteral("/usr/sbin/smbd"))) {
        if (QMessageBox::question(parent, tr7("File and Printer Sharing"),
                                  tr7("File sharing needs the Samba server (the samba package), which is not installed.\n\nInstall it now?"))
            != QMessageBox::Yes) {
            return false;
        }
        install = true;
    }
    static const char *script = R"(set -e
D="$1"; F="$2"; P="$3"; S="$4"; W="$5"; U="$6"; H="$7"; I="$8"
if [ "$I" = 1 ]; then
  if command -v apt-get >/dev/null; then DEBIAN_FRONTEND=noninteractive apt-get install -y samba
  elif command -v pacman >/dev/null; then pacman -S --noconfirm --needed samba
  else dnf install -y samba; fi
fi
# the Samba service: smbd on Ubuntu, smb on Arch and Fedora
SMB=smbd; systemctl cat smbd.service >/dev/null 2>&1 || SMB=smb
if [ "$D" = 1 ]; then systemctl enable --now avahi-daemon.socket avahi-daemon.service
else systemctl disable --now avahi-daemon.socket avahi-daemon.service; fi
C=/etc/samba/smb.conf
[ -f "$C" ] || [ ! -x /usr/sbin/smbd ] || printf '[global]\n   workgroup = WORKGROUP\n   server string = Samba\n' > "$C"   # Arch has none
if [ -f "$C" ]; then
  [ -f "$C.bak-win7" ] || cp "$C" "$C.bak-win7"
  grep -q '^include = /etc/samba/seven-global.conf' "$C" || sed -i '0,/^\[global\]/s//[global]\ninclude = \/etc\/samba\/seven-global.conf/' "$C"
  grep -q '^include = /etc/samba/seven-shares.conf' "$C" || printf '\ninclude = /etc/samba/seven-shares.conf\n' >> "$C"
fi
if [ "$S" = 1 ]; then printf '# seven-control: Advanced sharing settings, 128-bit encryption\n' > /etc/samba/seven-global.conf
else printf '# seven-control: Advanced sharing settings, devices with 40- or 56-bit encryption\nserver min protocol = NT1\n' > /etc/samba/seven-global.conf; fi
if [ "$P" = 1 ]; then
  G=yes; [ "$W" = 1 ] && G=no
  printf '[Public]\n   comment = Public\n   path = %s\n   read only = no\n   guest ok = %s\n   force user = %s\n' "$H" "$G" "$U" > /etc/samba/seven-shares.conf
else : > /etc/samba/seven-shares.conf; fi
if [ "$F" = 1 ] || [ "$P" = 1 ]; then
  systemctl enable "$SMB"; systemctl restart "$SMB"
  # Fedora: the firewall lets Samba in, SELinux lets it share the home folder
  if command -v firewall-cmd >/dev/null && firewall-cmd --state >/dev/null 2>&1; then
    firewall-cmd --permanent --add-service=samba >/dev/null && firewall-cmd --reload >/dev/null; fi
  if command -v setsebool >/dev/null && command -v selinuxenabled >/dev/null && selinuxenabled; then setsebool -P samba_enable_home_dirs on; fi
elif [ -x /usr/sbin/smbd ]; then systemctl disable --now "$SMB"; fi
)";
    const QString publicDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/Public");
    QDir().mkpath(publicDir);
    auto flag = [](bool b) {
        return b ? QStringLiteral("1") : QStringLiteral("0");
    };
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    QEventLoop loop;
    QObject::connect(&p, &QProcess::finished, &loop, &QEventLoop::quit);
    QGuiApplication::setOverrideCursor(Qt::BusyCursor);
    p.start(QStringLiteral("pkexec"), {QStringLiteral("/bin/sh"), QStringLiteral("-c"), QString::fromUtf8(script), QStringLiteral("sh"), flag(want.discovery),
                                       flag(want.files), flag(want.publicFolder), flag(want.strong), flag(want.password), qEnvironmentVariable("USER"), publicDir,
                                       flag(install)});
    if (p.waitForStarted()) {
        loop.exec();
    }
    QGuiApplication::restoreOverrideCursor();
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() == 126 || p.exitCode() == 127) {
        return false; // the password prompt was cancelled
    }
    if (p.exitCode() != 0) {
        QMessageBox::warning(parent, tr7("Advanced sharing settings"),
                             tr7("The settings could not be applied.\n\n%1").arg(QString::fromUtf8(p.readAll()).trimmed().right(600)));
        return false;
    }
    return true;
}

/** After the network location changed: that profile's settings come into effect */
inline void applyForLocation(QWidget *parent, const QString &location)
{
    apply(parent, stored(profileOf(location)));
}

inline const bool hooked = [] {
    Win7Net::locationChanged() = applyForLocation;
    return true;
}();

/* ---------------- the page ---------------- */

/** "Network discovery ─────────" inside a profile */
inline QWidget *section(QVBoxLayout *v, const QString &title)
{
    auto *h = new SectionHeader(title);
    v->addSpacing(8);
    v->addWidget(h);
    return h;
}

class SharingPage : public QWidget
{
public:
    std::function<void()> back;

    SharingPage()
    {
        setObjectName(QStringLiteral("win7sharing"));
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral("#win7sharing, #win7sharingbody { background: #ffffff; }"
                                     "#win7footer { background: #f0f0f0; border-top: 1px solid #dfdfdf; }"));
        m_current = currentProfile();

        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        auto *scroll = new QScrollArea;
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *body = new QWidget;
        body->setObjectName(QStringLiteral("win7sharingbody"));
        body->setAttribute(Qt::WA_StyledBackground, true);
        auto *bh = new QHBoxLayout(body);
        bh->setContentsMargins(20, 0, 20, 0);
        auto *content = new QWidget;
        content->setFixedWidth(m_width);
        bh->addStretch(1);
        bh->addWidget(content);
        bh->addStretch(1);
        auto *v = new QVBoxLayout(content);
        v->setContentsMargins(0, 22, 0, 24);
        v->setSpacing(0);
        v->addWidget(Win7Net::blueTitle(tr7("Change sharing options for different network profiles")));
        v->addSpacing(6);
        v->addWidget(Win7Net::plain(tr7("Windows creates a separate network profile for each network you use. You can choose specific options for each profile.")));
        v->addSpacing(12);
        for (const QString &profile : {QStringLiteral("home"), QStringLiteral("public")}) {
            buildProfile(v, profile);
        }
        v->addStretch(1);
        scroll->setWidget(body);
        outer->addWidget(scroll, 1);

        // grey band with "Save changes" (shield) and "Cancel" under the content's right edge
        auto *footer = new QWidget;
        footer->setObjectName(QStringLiteral("win7footer"));
        footer->setAttribute(Qt::WA_StyledBackground, true);
        footer->setFixedHeight(43);
        auto *fh = new QHBoxLayout(footer);
        fh->setContentsMargins(20, 10, 20, 9);
        auto *fc = new QWidget;
        fc->setFixedWidth(m_width);
        auto *fch = new QHBoxLayout(fc);
        fch->setContentsMargins(0, 0, 0, 0);
        fch->setSpacing(8);
        fch->addStretch(1);
        m_save = new QPushButton(tr7("Save changes"));
        m_save->setIcon(QIcon::fromTheme(QStringLiteral("seven-cp-shield")));
        m_save->setEnabled(false);
        auto *cancel = new QPushButton(tr7("Cancel"));
        cancel->setMinimumWidth(74);
        fch->addWidget(m_save);
        fch->addWidget(cancel);
        fh->addStretch(1);
        fh->addWidget(fc);
        fh->addStretch(1);
        outer->addWidget(footer);

        connect(m_save, &QPushButton::clicked, this, [this] {
            save();
        });
        connect(cancel, &QPushButton::clicked, this, [this] {
            if (back) {
                back();
            }
        });
    }

private:
    struct Ui {
        QRadioButton *discovery, *files, *publicFolder, *strong, *password, *homegroup = nullptr;
        Settings value() const
        {
            Settings s;
            s.discovery = discovery->isChecked();
            s.files = files->isChecked();
            s.publicFolder = publicFolder->isChecked();
            s.strong = strong->isChecked();
            s.password = password->isChecked();
            s.homegroupWindows = homegroup ? homegroup->isChecked() : true;
            return s;
        }
    };

    /** Two choices; returns the first ("on") one */
    QRadioButton *choice(QVBoxLayout *v, const QString &on, const QString &off, bool isOn)
    {
        auto *group = new QButtonGroup(this);
        QRadioButton *first = nullptr;
        v->addSpacing(9);
        for (const QString &text : {on, off}) {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(50, 0, 0, 0);
            auto *r = new QRadioButton;
            // the text wraps under itself, like the long Public folder choices
            auto *l = new QLabel(text);
            l->setWordWrap(true);
            l->setBuddy(r);
            row->setSpacing(4);
            row->addWidget(r, 0, Qt::AlignTop);
            row->addWidget(l, 1);
            l->installEventFilter(this);
            m_labelRadio.insert(l, r);
            group->addButton(r);
            v->addLayout(row);
            if (!first) {
                first = r;
                r->setChecked(isOn);
            } else {
                r->setChecked(!isOn);
            }
            connect(r, &QRadioButton::toggled, this, [this] {
                m_save->setEnabled(true);
            });
        }
        return first;
    }

    QLabel *para(QVBoxLayout *v, const QString &text, const QString &linkText = {}, std::function<void()> link = {})
    {
        v->addSpacing(9);
        QString html = text.toHtmlEscaped();
        if (!linkText.isEmpty()) {
            html += QStringLiteral(" <a href=\"#\" style=\"color:#0066cc;\">%1</a>").arg(linkText.toHtmlEscaped());
        }
        auto *l = new QLabel(html);
        l->setWordWrap(true);
        l->setContentsMargins(25, 0, 0, 0);
        if (link) {
            connect(l, &QLabel::linkActivated, this, [link] {
                link();
            });
        }
        v->addWidget(l);
        return l;
    }

    void buildProfile(QVBoxLayout *v, const QString &profile)
    {
        const bool current = profile == m_current;
        const Settings s = current ? systemState(stored(profile)) : stored(profile);

        // "Home or Work ─────────── (v)"
        auto *head = new QHBoxLayout;
        head->setSpacing(0);
        QString name = profile == QLatin1String("home") ? tr7("Home or Work") : tr7("Public");
        if (current) {
            name += tr7(" (current profile)");
        }
        head->addWidget(new QLabel(name));
        head->addWidget(new Win7Action::Rule, 1);
        auto *chevron = new Win7Action::Chevron;
        head->addWidget(chevron);
        v->addSpacing(8);
        v->addLayout(head);

        auto *box = new QWidget;
        auto *bv = new QVBoxLayout(box);
        bv->setContentsMargins(23, 0, 0, 10);
        bv->setSpacing(0);
        Ui ui;
        section(bv, tr7("Network discovery"));
        para(bv, tr7("When network discovery is on, this computer can see other network computers and devices and is visible to other network computers."),
             tr7("What is network discovery?"), [this] {
                 QMessageBox::information(this, tr7("Network discovery"),
                                          tr7("Network discovery lets this computer see other computers and devices on the network (the Network folder) and be visible to them.\n\nOn this system it is provided by the Avahi service."));
             });
        ui.discovery = choice(bv, tr7("Turn on network discovery"), tr7("Turn off network discovery"), s.discovery);

        section(bv, tr7("File and Printer Sharing"));
        para(bv, tr7("When file and printer sharing is on, files and printers that you have shared from this computer can be accessed by people on the network."));
        ui.files = choice(bv, tr7("Turn on file and printer sharing"), tr7("Turn off file and printer sharing"),
                          s.files);

        section(bv, tr7("Public folder sharing"));
        para(bv, tr7("When Public folder sharing is on, people on the network, including homegroup members, can access files in the Public folders."),
             tr7("What are the Public folders?"), [this] {
                 QMessageBox::information(this, tr7("Public folders"),
                                          tr7("The Public folder is the folder \"%1\". Files you put there can be opened from other computers on the network when Public folder sharing is on.")
                                              .arg(QDir::toNativeSeparators(QDir::homePath() + QStringLiteral("/Public"))));
             });
        ui.publicFolder = choice(bv,
                                 tr7("Turn on sharing so anyone with network access can read and write files in the Public folders"),
                                 tr7("Turn off Public folder sharing (people logged on to this computer can still access these folders)"),
                                 s.publicFolder);

        section(bv, tr7("Media streaming"));
        para(bv, tr7("When media streaming is on, people and devices on the network can access pictures, music, and videos on this computer. This computer can also find media on the network."));
        auto *media = new QVBoxLayout;
        media->setContentsMargins(50, 9, 0, 0);
        media->setSpacing(1);
        if (profile == QLatin1String("home")) {
            media->addWidget(new QLabel(tr7("Media streaming is off.")));
        }
        auto *mediaLink = new Link(tr7("Choose media streaming options..."), [this] {
            QMessageBox::information(this, tr7("Media streaming options"),
                                     tr7("Media streaming needs a DLNA media server (for example, the minidlna package), which is not installed."));
        });
        mediaLink->setWordWrap(false);
        media->addWidget(mediaLink, 0, Qt::AlignLeft);
        bv->addLayout(media);

        section(bv, tr7("File sharing connections"));
        para(bv, tr7("Windows 7 uses 128-bit encryption to help protect file sharing connections. Some devices don't support 128-bit encryption and must use 40- or 56-bit encryption."));
        ui.strong = choice(bv, tr7("Use 128-bit encryption to help protect file sharing connections (recommended)"),
                           tr7("Enable file sharing for devices that use 40- or 56-bit encryption"), s.strong);

        section(bv, tr7("Password protected sharing"));
        para(bv, tr7("When password protected sharing is on, only people who have a user account and password on this computer can access shared files, printers attached to this computer, and the Public folders. To give other people access, you must turn off password protected sharing."));
        ui.password = choice(bv, tr7("Turn on password protected sharing"), tr7("Turn off password protected sharing"),
                             s.password);

        if (profile == QLatin1String("home")) {
            section(bv, tr7("HomeGroup connections"));
            para(bv, tr7("Typically, Windows manages the connections to other homegroup computers. But if you have the same user accounts and passwords on all of your computers, you can have HomeGroup use your account instead."),
                 tr7("Help me choose"), [this] {
                     QMessageBox::information(this, tr7("HomeGroup connections"),
                                              tr7("If you don't know what to choose, keep the recommended option: Windows will manage homegroup connections itself."));
                 });
            ui.homegroup = choice(bv, tr7("Allow Windows to manage homegroup connections (recommended)"),
                                  tr7("Use user accounts and passwords to connect to other computers"), s.homegroupWindows);
        }
        v->addWidget(box);

        // the current profile is open, the other one closed
        const bool open = current || (m_current.isEmpty() && profile == QLatin1String("home"));
        box->setVisible(open);
        chevron->expanded = open;
        chevron->clicked = [chevron, box] {
            chevron->expanded = !chevron->expanded;
            box->setVisible(chevron->expanded);
            chevron->update();
        };
        m_ui.insert(profile, ui);
    }

    bool eventFilter(QObject *o, QEvent *e) override
    {
        // a click on a choice's text picks it, as on the real page
        if (e->type() == QEvent::MouseButtonRelease) {
            if (QRadioButton *r = m_labelRadio.value(static_cast<QLabel *>(o))) {
                r->setChecked(true);
                r->setFocus();
            }
        }
        return QWidget::eventFilter(o, e);
    }

    void save()
    {
        for (auto it = m_ui.cbegin(); it != m_ui.cend(); ++it) {
            if (it.key() == m_current && !apply(this, it.value().value())) {
                return; // stays on the page, like after "No" in the UAC prompt
            }
        }
        for (auto it = m_ui.cbegin(); it != m_ui.cend(); ++it) {
            store(it.key(), it.value().value());
        }
        if (back) {
            back();
        }
    }

    const int m_width = 580;
    QString m_current;
    QMap<QString, Ui> m_ui;
    QHash<QLabel *, QRadioButton *> m_labelRadio;
    QPushButton *m_save = nullptr;
};
}
