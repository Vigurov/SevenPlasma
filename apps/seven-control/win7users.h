/*
 * "User Accounts" of the Windows 7 Control Panel: your account, managing other accounts, creating
 * and deleting accounts, passwords, names, account types, pictures, the Guest account and the
 * "Environment Variables" dialog.
 *
 *   accounts         -> AccountsService (org.freedesktop.Accounts on the system bus); polkit asks for
 *                       the administrator password where Windows 7 shows the UAC shield
 *   own password     -> passwd in a pseudo terminal (asks the current password like Windows 7),
 *                       the current password is checked with unix_chkpwd
 *   pictures         -> the 36 Windows 7 user tiles in ~/.local/share/seven-control/account-pictures,
 *                       set with SetIconFile (+ ~/.face for your own account, read by the Start menu)
 *   Guest            -> a normal account "guest" without a password, locked while "turned off"
 *   variables        -> ~/.config/environment.d/90-win7.conf and /etc/environment
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7chrome.h"
#include "win7display.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QTabWidget>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTreeWidget>

#include <unistd.h>

namespace Win7Users
{
enum {
    GuestView = 19,
    HomeView = 20,
    ManageView = 21,
    CreateView = 22,
    AccountView = 23,
    PasswordView = 24,
    RemovePasswordView = 25,
    NameView = 26,
    TypeView = 27,
    PictureView = 28,
    DeleteView = 29,
};

struct Nav {
    std::function<void(int)> open;
    std::function<void()> home;
    std::function<void()> back;
};

/* ---------------- AccountsService ---------------- */

const QString kService = QStringLiteral("org.freedesktop.Accounts");
const QString kUserIface = QStringLiteral("org.freedesktop.Accounts.User");

struct Account {
    QString path;
    qint64 uid = -1;
    QString user;
    QString real;
    QString icon;
    int type = 0;     // 0 standard, 1 administrator
    int pwMode = 0;   // 0 has a password, 1 set at next login, 2 none
    bool locked = false;
    QString hint;
    bool valid() const
    {
        return !path.isEmpty();
    }
    QString name() const
    {
        return real.isEmpty() ? user : real;
    }
    bool isGuest() const
    {
        return user == QLatin1String("guest");
    }
    bool hasPassword() const
    {
        return pwMode != 2;
    }
};

inline Account account(const QString &path)
{
    Account a;
    QDBusInterface u(kService, path, kUserIface, QDBusConnection::systemBus());
    if (!u.isValid()) {
        return a;
    }
    a.path = path;
    a.uid = u.property("Uid").toLongLong();
    a.user = u.property("UserName").toString();
    a.real = u.property("RealName").toString();
    a.icon = u.property("IconFile").toString();
    a.type = u.property("AccountType").toInt();
    a.pwMode = u.property("PasswordMode").toInt();
    a.locked = u.property("Locked").toBool();
    a.hint = u.property("PasswordHint").toString();
    return a;
}

inline QString findUser(const QString &name)
{
    QDBusInterface acc(kService, QStringLiteral("/org/freedesktop/Accounts"), kService, QDBusConnection::systemBus());
    const QDBusReply<QDBusObjectPath> r = acc.call(QStringLiteral("FindUserByName"), name);
    return r.isValid() ? r.value().path() : QString();
}

inline Account self()
{
    QDBusInterface acc(kService, QStringLiteral("/org/freedesktop/Accounts"), kService, QDBusConnection::systemBus());
    const QDBusReply<QDBusObjectPath> r = acc.call(QStringLiteral("FindUserById"), qint64(getuid()));
    return r.isValid() ? account(r.value().path()) : Account();
}

/** people's accounts (not system ones), the Guest account apart */
inline QList<Account> accounts()
{
    QList<Account> list;
    QDBusInterface acc(kService, QStringLiteral("/org/freedesktop/Accounts"), kService, QDBusConnection::systemBus());
    const QDBusReply<QList<QDBusObjectPath>> r = acc.call(QStringLiteral("ListCachedUsers"));
    if (r.isValid()) {
        for (const QDBusObjectPath &p : r.value()) {
            const Account a = account(p.path());
            if (a.valid() && !a.isGuest()) {
                list.append(a);
            }
        }
    }
    std::sort(list.begin(), list.end(), [](const Account &a, const Account &b) {
        return a.uid < b.uid;
    });
    return list;
}

/** a method call that may ask polkit for the administrator password; returns the error text */
inline QString call(const QString &path, const QString &iface, const QString &method, const QVariantList &args)
{
    QDBusMessage m = QDBusMessage::createMethodCall(kService, path, iface, method);
    m.setArguments(args);
    m.setInteractiveAuthorizationAllowed(true);
    const QDBusMessage r = QDBusConnection::systemBus().call(m, QDBus::BlockWithGui, 300000);
    if (r.type() == QDBusMessage::ErrorMessage) {
        return r.errorName().contains(QLatin1String("PermissionDenied")) || r.errorName().contains(QLatin1String("NotAuthorized")) ? QStringLiteral("cancelled")
                                                                                                                                     : r.errorMessage();
    }
    return QString();
}

inline QString userCall(const Account &a, const QString &method, const QVariantList &args)
{
    return call(a.path, kUserIface, method, args);
}

/** shows what went wrong, unless the password prompt was just cancelled; true on success */
inline bool done(QWidget *parent, const QString &error)
{
    if (error.isEmpty()) {
        return true;
    }
    if (error != QLatin1String("cancelled")) {
        QMessageBox::warning(parent, tr7("User Accounts"), tr7("The changes could not be made.\n\n%1").arg(error));
    }
    return false;
}

inline QString typeText(const Account &a)
{
    return a.type == 1 ? tr7("Administrator") : tr7("Standard user");
}

inline QStringList statusLines(const Account &a)
{
    if (a.isGuest()) {
        return {a.locked ? tr7("Guest account is off") : tr7("Guest account is on")};
    }
    QStringList l{typeText(a)};
    if (a.locked) {
        l.append(tr7("Account is disabled"));
    } else if (a.hasPassword()) {
        l.append(tr7("Password protected"));
    }
    return l;
}

inline int adminCount()
{
    int n = 0;
    for (const Account &a : accounts()) {
        n += a.type == 1 && !a.locked ? 1 : 0;
    }
    return n;
}

inline QString picturesDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/seven-control/account-pictures");
}

/** Windows 7 order: usertile10 ... usertile44, then the orange gerbera */
inline QStringList stockPictures()
{
    QStringList list;
    for (int i = 10; i <= 44; ++i) {
        const QString f = picturesDir() + QStringLiteral("/usertile%1.png").arg(i);
        if (QFile::exists(f)) {
            list.append(f);
        }
    }
    if (QFile::exists(picturesDir() + QStringLiteral("/user.png"))) {
        list.append(picturesDir() + QStringLiteral("/user.png"));
    }
    return list;
}

inline QPixmap picture(const Account &a)
{
    QPixmap pm;
    if (!a.icon.isEmpty()) {
        pm.load(a.icon);
    }
    if (pm.isNull()) {
        pm.load(picturesDir() + (a.isGuest() ? QStringLiteral("/guest.png") : QStringLiteral("/user.png")));
    }
    return pm;
}

/** a square 128 px PNG of any picture (cropped to the middle), for SetIconFile */
inline QString squarePicture(const QString &file)
{
    QImage img(file);
    if (img.isNull()) {
        return QString();
    }
    const int side = qMin(img.width(), img.height());
    img = img.copy((img.width() - side) / 2, (img.height() - side) / 2, side, side).scaled(128, 128, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QString out = QDir::tempPath() + QStringLiteral("/seven-usertile-%1.png").arg(getpid());
    img.save(out);
    return out;
}

inline bool setPicture(QWidget *parent, const Account &a, const QString &file)
{
    const QString png = file.startsWith(picturesDir()) ? file : squarePicture(file);
    if (png.isEmpty() || !done(parent, userCall(a, QStringLiteral("SetIconFile"), {png}))) {
        return false;
    }
    if (a.uid == qint64(getuid())) { // the Start menu and the lock screen read ~/.face
        const QString face = QDir::homePath() + QStringLiteral("/.face");
        QFile::remove(face);
        QFile::copy(png, face);
        QFile::setPermissions(face, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    }
    return true;
}

/** SHA-512 crypt of a password for SetPassword */
inline QString cryptPassword(const QString &pw)
{
    QProcess p;
    p.start(QStringLiteral("openssl"), {QStringLiteral("passwd"), QStringLiteral("-6"), QStringLiteral("-stdin")});
    p.write(pw.toUtf8() + '\n');
    p.closeWriteChannel();
    p.waitForFinished(5000);
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

/** checks your own password with the PAM helper (no administrator rights needed) */
inline bool checkOwnPassword(const QString &pw)
{
    QProcess p;
    p.start(QStringLiteral("/usr/sbin/unix_chkpwd"), {QString::fromLocal8Bit(qgetenv("USER")), QStringLiteral("nullok")});
    p.write(pw.toUtf8() + '\0');
    p.closeWriteChannel();
    p.waitForFinished(5000);
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

/** your own password through passwd in a pseudo terminal; returns an error text or empty */
inline QString changeOwnPassword(const QString &current, const QString &fresh)
{
    static const char *script = R"py(
import os, pty, select, sys
old = os.environ.get('W7_OLD', ''); new = os.environ.get('W7_NEW', '')
pid, fd = pty.fork()
if pid == 0:
    os.environ['LC_ALL'] = 'C'
    os.execvp('passwd', ['passwd'])
out = b''; buf = b''; asked_new = 0
while True:
    r, _, _ = select.select([fd], [], [], 20)
    if not r:
        break
    try:
        data = os.read(fd, 1024)
    except OSError:
        break
    if not data:
        break
    out += data; buf += data; low = buf.lower()
    if b'retype' in low and low.rstrip().endswith(b':'):
        os.write(fd, (new + '\n').encode()); buf = b''
    elif b'new password' in low and low.rstrip().endswith(b':'):
        asked_new += 1
        if asked_new > 1:
            os.kill(pid, 9); break
        os.write(fd, (new + '\n').encode()); buf = b''
    elif b'password' in low and low.rstrip().endswith(b':'):
        os.write(fd, (old + '\n').encode()); buf = b''
try:
    _, st = os.waitpid(pid, 0); code = os.waitstatus_to_exitcode(st)
except ChildProcessError:
    code = 1
sys.stdout.write(out.decode(errors='replace'))
sys.exit(code)
)py";
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("W7_OLD"), current);
    env.insert(QStringLiteral("W7_NEW"), fresh);
    p.setProcessEnvironment(env);
    p.start(QStringLiteral("python3"), {QStringLiteral("-c"), QString::fromLatin1(script)});
    p.waitForFinished(60000);
    const QString out = QString::fromUtf8(p.readAllStandardOutput()).toLower();
    if (p.exitCode() == 0 && out.contains(QLatin1String("success"))) {
        return QString();
    }
    if (out.contains(QLatin1String("authentication token manipulation")) || out.contains(QLatin1String("authentication failure"))) {
        return tr7("The current password is not correct.");
    }
    if (out.contains(QLatin1String("longer")) || out.contains(QLatin1String("short"))) {
        return tr7("The password is too short. Enter at least 6 characters.");
    }
    if (out.contains(QLatin1String("simple")) || out.contains(QLatin1String("similar")) || out.contains(QLatin1String("palindrome")) || out.contains(QLatin1String("case"))
        || out.contains(QLatin1String("unchanged")) || out.contains(QLatin1String("rotated"))) {
        return tr7("The password is too simple or too similar to the previous one. Choose another password.");
    }
    return tr7("The password could not be changed.");
}

/** login name from what was typed: Latin letters, digits and "-" (Ukrainian letters are transliterated) */
inline QString loginName(const QString &shown)
{
    static const QHash<QChar, QString> tr{
        {QChar(0x430), QStringLiteral("a")}, {QChar(0x431), QStringLiteral("b")}, {QChar(0x432), QStringLiteral("v")}, {QChar(0x433), QStringLiteral("h")},
        {QChar(0x491), QStringLiteral("g")}, {QChar(0x434), QStringLiteral("d")}, {QChar(0x435), QStringLiteral("e")}, {QChar(0x454), QStringLiteral("ie")},
        {QChar(0x436), QStringLiteral("zh")}, {QChar(0x437), QStringLiteral("z")}, {QChar(0x438), QStringLiteral("y")}, {QChar(0x456), QStringLiteral("i")},
        {QChar(0x457), QStringLiteral("i")}, {QChar(0x439), QStringLiteral("i")}, {QChar(0x43a), QStringLiteral("k")}, {QChar(0x43b), QStringLiteral("l")},
        {QChar(0x43c), QStringLiteral("m")}, {QChar(0x43d), QStringLiteral("n")}, {QChar(0x43e), QStringLiteral("o")}, {QChar(0x43f), QStringLiteral("p")},
        {QChar(0x440), QStringLiteral("r")}, {QChar(0x441), QStringLiteral("s")}, {QChar(0x442), QStringLiteral("t")}, {QChar(0x443), QStringLiteral("u")},
        {QChar(0x444), QStringLiteral("f")}, {QChar(0x445), QStringLiteral("kh")}, {QChar(0x446), QStringLiteral("ts")}, {QChar(0x447), QStringLiteral("ch")},
        {QChar(0x448), QStringLiteral("sh")}, {QChar(0x449), QStringLiteral("shch")}, {QChar(0x44c), QString()}, {QChar(0x44e), QStringLiteral("iu")},
        {QChar(0x44f), QStringLiteral("ia")}, {QChar(0x44b), QStringLiteral("y")}, {QChar(0x44d), QStringLiteral("e")}, {QChar(0x451), QStringLiteral("e")},
        {QChar(0x44a), QString()}, {QChar(0x2019), QString()}, {QChar('\''), QString()}};
    QString out;
    for (const QChar c : shown.toLower()) {
        if (tr.contains(c)) {
            out += tr.value(c);
        } else if ((c >= QLatin1Char('a') && c <= QLatin1Char('z')) || c.isDigit()) {
            out += c;
        } else if (!out.isEmpty() && !out.endsWith(QLatin1Char('-'))) {
            out += QLatin1Char('-');
        }
    }
    while (out.endsWith(QLatin1Char('-'))) {
        out.chop(1);
    }
    if (out.isEmpty() || out.at(0).isDigit()) {
        out.prepend(QStringLiteral("user"));
    }
    out = out.left(28);
    QString name = out;
    for (int i = 2; !findUser(name).isEmpty(); ++i) {
        name = out + QString::number(i);
    }
    return name;
}

/* ---------------- account pictures and tiles ---------------- */

/** the picture in the glossy Windows 7 frame */
inline QPixmap framedPicture(const QPixmap &src, int inner)
{
    const int pad = qMax(4, inner / 10);
    const int side = inner + pad * 2;
    QPixmap pm(side + 2, side + 3);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    // soft shadow
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 28));
    p.drawRoundedRect(QRectF(1.5, 2.5, side, side), 4, 4);
    // frame
    QLinearGradient g(0, 0, 0, side);
    g.setColorAt(0, QColor(0xfd, 0xfe, 0xff));
    g.setColorAt(0.5, QColor(0xe4, 0xec, 0xf5));
    g.setColorAt(1, QColor(0xc9, 0xd5, 0xe3));
    p.setBrush(g);
    p.setPen(QPen(QColor(0x8e, 0x9e, 0xb2), 1));
    p.drawRoundedRect(QRectF(0.5, 0.5, side - 1, side - 1), 4, 4);
    const QRectF in(pad, pad, inner, inner);
    p.drawPixmap(in, src.scaled(inner * 2, inner * 2, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation), QRectF());
    p.setPen(QPen(QColor(0x9a, 0xa6, 0xb4), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(in.adjusted(-0.5, -0.5, 0.5, 0.5));
    // gloss over the upper half
    QLinearGradient gloss(0, pad, 0, pad + inner / 2.0);
    gloss.setColorAt(0, QColor(255, 255, 255, 70));
    gloss.setColorAt(1, QColor(255, 255, 255, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(gloss);
    p.drawRect(QRectF(pad, pad, inner, inner / 2.0));
    return pm;
}

/** "Дмитрий / Администратор / Защищена паролем" box; clickable when `open` is set */
class AccountTile : public QWidget
{
public:
    AccountTile(const Account &a, std::function<void()> open = {}, bool framed = true)
        : m_open(std::move(open))
        , m_framed(framed)
    {
        setAttribute(Qt::WA_Hover);
        if (m_open) {
            setCursor(Qt::PointingHandCursor);
        }
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(10, 8, 18, 8);
        h->setSpacing(12);
        auto *pic = new QLabel;
        pic->setPixmap(framedPicture(picture(a), 48));
        h->addWidget(pic, 0, Qt::AlignVCenter);
        auto *v = new QVBoxLayout;
        v->setSpacing(0);
        auto *name = new QLabel(a.name());
        QFont f = name->font();
        f.setPixelSize(15);
        name->setFont(f);
        v->addWidget(name);
        for (const QString &line : statusLines(a)) {
            v->addWidget(new QLabel(line));
        }
        h->addLayout(v, 1);
        setMinimumWidth(250);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const bool hot = m_open && underMouse();
        if (!m_framed && !hot) {
            return;
        }
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, hot ? QColor(0xf2, 0xf8, 0xfe) : QColor(0xfb, 0xfd, 0xff));
        g.setColorAt(1, hot ? QColor(0xdc, 0xeb, 0xfc) : QColor(0xec, 0xf3, 0xfb));
        p.setBrush(g);
        p.setPen(hot ? QColor(0x7d, 0xa2, 0xce) : QColor(0xd2, 0xdf, 0xec));
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
        if (e->button() == Qt::LeftButton && m_open && rect().contains(e->pos())) {
            m_open();
        }
    }

private:
    std::function<void()> m_open;
    bool m_framed;
};

/* ---------------- automatic logon (SDDM) ----------------
 * Windows 7 logs on by itself when the computer has a single account without a password ("implicit" here), or
 * when "Users must enter a user name and password" is cleared in netplwiz. The SDDM equivalent is an
 * [Autologin] section in /etc/sddm.conf.d/zz-seven-autologin.conf (written as root, one password prompt). */

inline QString autologinPath()
{
    return QStringLiteral("/etc/sddm.conf.d/zz-seven-autologin.conf");
}

struct Autologin {
    QString user;
    bool implicit = false; // set because the only account lost its password, not chosen in netplwiz
};

inline Autologin autologin()
{
    Autologin a;
    QFile f(autologinPath());
    if (!f.open(QIODevice::ReadOnly)) {
        return a;
    }
    const QString t = QString::fromUtf8(f.readAll());
    const auto m = QRegularExpression(QStringLiteral("^User=(.*)$"), QRegularExpression::MultilineOption).match(t);
    a.user = m.hasMatch() ? m.captured(1).trimmed() : QString();
    a.implicit = t.contains(QStringLiteral("seven-implicit"));
    return a;
}

/** the session the user is in now (Plasma X11 with the Aero shell here), as SDDM names it */
inline QString autologinSession()
{
    const QString s = qEnvironmentVariable("DESKTOP_SESSION");
    for (const char *dir : {"/usr/share/xsessions/", "/usr/share/wayland-sessions/"}) {
        if (!s.isEmpty() && QFile::exists(QString::fromLatin1(dir) + s + QStringLiteral(".desktop"))) {
            return s + QStringLiteral(".desktop");
        }
    }
    return QStringLiteral("plasma.desktop");
}

/** as root in one pkexec: optionally `passwd -d <removePasswordOf>` first, then write (user set) or delete
    (user empty) the autologin file. False if cancelled or failed. */
inline bool setAutologin(QWidget *parent, const QString &user, bool implicit, const QString &removePasswordOf = QString())
{
    QString content;
    if (!user.isEmpty()) {
        content = QStringLiteral("# Windows 7 automatic logon (seven-control%1)\n[Autologin]\nUser=%2\nSession=%3\nRelogin=false\n")
                      .arg(implicit ? QStringLiteral(", seven-implicit: the only account has no password") : QStringLiteral(", netplwiz"), user, autologinSession());
    }
    const QString script = QStringLiteral(
        "set -e\n"
        "if [ -n \"$3\" ]; then passwd -d -- \"$3\" >/dev/null; fi\n"
        "if [ -n \"$1\" ]; then mkdir -p /etc/sddm.conf.d; printf '%s' \"$1\" > \"$2\"; chmod 644 \"$2\"; else rm -f \"$2\"; fi\n");
    const int code = QProcess::execute(QStringLiteral("pkexec"), {QStringLiteral("/bin/sh"), QStringLiteral("-c"), script, QStringLiteral("sh"), content, autologinPath(), removePasswordOf});
    if (code == 126 || code == 127) {
        return false; // the password prompt was cancelled
    }
    if (code != 0) {
        QMessageBox::warning(parent, tr7("User Accounts"), tr7("The logon settings could not be changed."));
        return false;
    }
    return true;
}

/** after a password was given to `a`: an implicit automatic logon of that account stops, as in Windows 7 */
inline void dropImplicitAutologin(QWidget *parent, const Account &a)
{
    const Autologin al = autologin();
    if (al.implicit && al.user == a.user) {
        setAutologin(parent, QString(), false);
    }
}

/* ---------------- page pieces ---------------- */

/** which account the next page is about (empty = your own) */
inline QString &target()
{
    static QString path;
    return path;
}

inline Account targetAccount()
{
    return target().isEmpty() ? self() : account(target());
}

inline bool isSelf(const Account &a)
{
    return a.uid == qint64(getuid());
}

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

inline QLabel *text(const QString &t)
{
    auto *l = new QLabel(t);
    l->setWordWrap(true);
    return l;
}

/** a link with the UAC shield in front of it */
inline QWidget *shieldLink(const QString &t, std::function<void()> open, bool shield = true)
{
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(4);
    auto *ic = new QLabel;
    ic->setFixedWidth(18);
    if (shield) {
        ic->setPixmap(QIcon::fromTheme(QStringLiteral("seven-cp-shield")).pixmap(16, 16));
    }
    h->addWidget(ic, 0, Qt::AlignTop);
    auto *l = new Link(t, std::move(open));
    l->setWordWrap(false);
    h->addWidget(l, 1);
    return w;
}

inline void info(QWidget *parent, const QString &caption, const QString &body)
{
    QMessageBox::information(parent, caption, body);
}

/** white page whose content sits in a column like the Windows 7 task pages; buttons under a full-width line */
struct TaskPage {
    QWidget *root;
    QVBoxLayout *body;
    QHBoxLayout *buttons;
};

inline TaskPage taskPage(bool withButtons = true)
{
    auto *root = new QWidget;
    root->setObjectName(QStringLiteral("win7page"));
    root->setAttribute(Qt::WA_StyledBackground, true);
    root->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
    auto *outer = new QVBoxLayout(root);
    outer->setContentsMargins(0, 22, 0, 20);
    outer->setSpacing(0);
    auto *row = new QHBoxLayout;
    row->addSpacing(150);
    auto *col = new QWidget;
    col->setMaximumWidth(640);
    auto *body = new QVBoxLayout(col);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(6);
    row->addWidget(col, 1);
    row->addStretch(0);
    row->addSpacing(80);
    outer->addLayout(row);
    QHBoxLayout *buttons = nullptr;
    if (withButtons) {
        outer->addSpacing(18);
        auto *line = new QFrame;
        line->setFixedHeight(1);
        line->setStyleSheet(QStringLiteral("background: #dfdfdf;"));
        outer->addWidget(line);
        outer->addSpacing(12);
        auto *brow = new QHBoxLayout;
        brow->addSpacing(150);
        auto *bcol = new QWidget;
        bcol->setMaximumWidth(640);
        buttons = new QHBoxLayout(bcol);
        buttons->setContentsMargins(0, 0, 0, 0);
        buttons->addStretch(1);
        brow->addWidget(bcol, 1);
        brow->addSpacing(80);
        outer->addLayout(brow);
    }
    outer->addStretch(1);
    return {root, body, buttons};
}

inline QPushButton *button(QHBoxLayout *row, const QString &t)
{
    auto *b = new QPushButton(t);
    b->setMinimumWidth(80);
    row->addWidget(b);
    return b;
}

/** picture + name + type lines, without the box (top of the task pages) */
inline QWidget *accountHead(const Account &a)
{
    return new AccountTile(a, {}, false);
}

inline QLineEdit *field(const QString &placeholder, bool password = false)
{
    auto *e = new QLineEdit;
    e->setPlaceholderText(placeholder);
    e->setFixedWidth(200);
    if (password) {
        e->setEchoMode(QLineEdit::Password);
    }
    return e;
}

inline void strongPasswordHelp(QWidget *parent)
{
    info(parent, tr7("Strong password"),
         tr7("A strong password:\n\n• is at least 8 characters long;\n• does not contain your user name, real name, or company name;\n• does not contain a complete word;\n• is significantly different from previous passwords;\n• contains uppercase and lowercase letters, numbers and symbols."));
}

inline void hintHelp(QWidget *parent)
{
    info(parent, tr7("Password hint"),
         tr7("A hint helps you remember the password. It is shown on the Welcome screen if the password is typed incorrectly.\n\nThe hint is visible to everyone who uses this computer, so don't write the password itself in it."));
}

inline void typeHelp(QWidget *parent)
{
    info(parent, tr7("Standard account"),
         tr7("A standard account helps protect your computer: programs and people can't change the system without an administrator password.\n\nA standard account is recommended for everyday use; to install programs and change system settings, just type the administrator password when asked."));
}

/** "Standard user / Administrator" radio buttons with the Windows 7 explanations */
inline QButtonGroup *typeChoice(QVBoxLayout *v, int current)
{
    auto *group = new QButtonGroup(v);
    auto add = [v, group](int id, const QString &label, const QString &desc) {
        auto *r = new QRadioButton(label);
        group->addButton(r, id);
        v->addWidget(r);
        auto *d = text(desc);
        d->setContentsMargins(20, 0, 0, 6);
        v->addWidget(d);
        return r;
    };
    add(0, tr7("Standard user"),
        tr7("Standard account users can use most software and change system settings that do not affect other users or the security of the computer."));
    add(1, tr7("Administrator"),
        tr7("Administrators have complete access to the computer and can make any desired changes. Based on notification settings, administrators may be asked to provide their password before making changes that affect other users."));
    if (group->button(current)) {
        group->button(current)->setChecked(true);
    }
    v->addSpacing(4);
    v->addWidget(text(tr7("We recommend that you protect every account with a strong password.")));
    return group;
}

/* ---------------- "Environment Variables" ---------------- */

inline QString userEnvFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/environment.d/90-win7.conf");
}

inline QList<QPair<QString, QString>> readEnv(const QString &file)
{
    QList<QPair<QString, QString>> list;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        return list;
    }
    for (QString line : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || !line.contains(QLatin1Char('='))) {
            continue;
        }
        if (line.startsWith(QLatin1String("export "))) {
            line = line.mid(7);
        }
        QString value = line.section(QLatin1Char('='), 1);
        if (value.size() >= 2 && ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))) || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))))) {
            value = value.mid(1, value.size() - 2);
        }
        list.append({line.section(QLatin1Char('='), 0, 0).trimmed(), value});
    }
    return list;
}

inline QByteArray envText(const QList<QPair<QString, QString>> &vars, bool quote)
{
    QByteArray out;
    for (const auto &v : vars) {
        out += (v.first + QLatin1Char('=') + (quote ? QLatin1Char('"') + v.second + QLatin1Char('"') : v.second) + QLatin1Char('\n')).toUtf8();
    }
    return out;
}

/** "New User Variable" / "Edit System Variable" */
inline bool editVariable(QWidget *parent, const QString &caption, QString &name, QString &value)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(caption);
    dlg.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    auto *g = new QGridLayout(&dlg);
    g->setContentsMargins(14, 14, 14, 12);
    auto *n = new QLineEdit(name);
    auto *v = new QLineEdit(value);
    n->setMinimumWidth(280);
    g->addWidget(new QLabel(tr7("Variable name:")), 0, 0);
    g->addWidget(n, 0, 1);
    g->addWidget(new QLabel(tr7("Variable value:")), 1, 0);
    g->addWidget(v, 1, 1);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    auto *ok = new QPushButton(QStringLiteral("OK"));
    auto *cancel = new QPushButton(tr7("Cancel"));
    ok->setDefault(true);
    row->addWidget(ok);
    row->addWidget(cancel);
    g->addLayout(row, 2, 0, 1, 2);
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    auto check = [ok, n] {
        ok->setEnabled(valid.match(n->text().trimmed()).hasMatch());
    };
    QObject::connect(n, &QLineEdit::textChanged, &dlg, check);
    check();
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    name = n->text().trimmed();
    value = v->text();
    return true;
}

class EnvironmentDialog : public QDialog
{
public:
    explicit EnvironmentDialog(QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(tr7("Environment Variables"));
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        setAttribute(Qt::WA_DeleteOnClose);
        setFixedSize(420, 470);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(12, 12, 12, 10);
        m_user = readEnv(userEnvFile());
        m_system = readEnv(QStringLiteral("/etc/environment"));
        m_userTree = group(v, tr7("User variables for %1", "env").arg(QString::fromLocal8Bit(qgetenv("USER"))), m_user, false);
        m_systemTree = group(v, tr7("System variables"), m_system, true);
        auto *row = new QHBoxLayout;
        row->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        ok->setDefault(true);
        row->addWidget(ok);
        row->addWidget(cancel);
        v->addLayout(row);
        connect(ok, &QPushButton::clicked, this, [this] {
            if (save()) {
                accept();
            }
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

private:
    QTreeWidget *group(QVBoxLayout *v, const QString &caption, QList<QPair<QString, QString>> &vars, bool system)
    {
        auto *box = new QGroupBox(caption);
        auto *bv = new QVBoxLayout(box);
        auto *tree = new QTreeWidget;
        tree->setHeaderLabels({tr7("Variable"), tr7("Value")});
        tree->setRootIsDecorated(false);
        tree->setColumnWidth(0, 120);
        bv->addWidget(tree);
        auto *row = new QHBoxLayout;
        row->addStretch(1);
        auto *add = new QPushButton(tr7("New..."));
        auto *edit = new QPushButton(tr7("Edit..."));
        auto *del = new QPushButton(tr7("Delete"));
        for (QPushButton *b : {add, edit, del}) {
            row->addWidget(b);
        }
        bv->addLayout(row);
        v->addWidget(box, 1);
        auto fill = [tree, &vars] {
            tree->clear();
            for (const auto &x : vars) {
                new QTreeWidgetItem(tree, {x.first, x.second});
            }
        };
        fill();
        auto *flag = system ? &m_systemChanged : &m_userChanged;
        const QString what = system ? tr7("System", "variable") : tr7("User");
        connect(add, &QPushButton::clicked, this, [this, &vars, fill, flag, what] {
            QString n, val;
            if (editVariable(this, tr7("New %1 Variable").arg(what), n, val)) {
                vars.removeIf([&n](const QPair<QString, QString> &x) {
                    return x.first == n;
                });
                vars.append({n, val});
                *flag = true;
                fill();
            }
        });
        auto change = [this, tree, &vars, fill, flag, what] {
            QTreeWidgetItem *it = tree->currentItem();
            if (!it) {
                return;
            }
            const int i = tree->indexOfTopLevelItem(it);
            QString n = vars.at(i).first, val = vars.at(i).second;
            if (editVariable(this, tr7("Edit %1 Variable").arg(what), n, val)) {
                vars[i] = {n, val};
                *flag = true;
                fill();
            }
        };
        connect(edit, &QPushButton::clicked, this, change);
        connect(tree, &QTreeWidget::itemDoubleClicked, this, change);
        connect(del, &QPushButton::clicked, this, [tree, &vars, fill, flag] {
            QTreeWidgetItem *it = tree->currentItem();
            if (!it) {
                return;
            }
            vars.removeAt(tree->indexOfTopLevelItem(it));
            *flag = true;
            fill();
        });
        return tree;
    }

    bool save()
    {
        if (m_userChanged) {
            QDir().mkpath(QFileInfo(userEnvFile()).path());
            QFile f(userEnvFile());
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write("# Windows 7 Control Panel > User Accounts > Environment Variables\n" + envText(m_user, false));
            }
        }
        if (m_systemChanged) {
            QTemporaryFile tmp;
            tmp.setAutoRemove(true);
            if (!tmp.open()) {
                return false;
            }
            tmp.write(envText(m_system, true));
            tmp.flush();
            // a copy of the old file stays next to it
            const int code = QProcess::execute(QStringLiteral("pkexec"),
                                               {QStringLiteral("/bin/sh"), QStringLiteral("-c"),
                                                QStringLiteral("cp /etc/environment /etc/environment.seven-backup && install -m 644 \"$1\" /etc/environment"), QStringLiteral("sh"),
                                                tmp.fileName()});
            if (code != 0) {
                return code == 126 || code == 127 ? false : (QMessageBox::warning(this, windowTitle(), tr7("The system variables could not be changed.")), false);
            }
        }
        if (m_userChanged || m_systemChanged) {
            QMessageBox::information(this, windowTitle(), tr7("The changes take effect at the next logon."));
        }
        return true;
    }

    QList<QPair<QString, QString>> m_user;
    QList<QPair<QString, QString>> m_system;
    QTreeWidget *m_userTree;
    QTreeWidget *m_systemTree;
    bool m_userChanged = false;
    bool m_systemChanged = false;
};

/* ---------------- the pages ---------------- */

/** "Make changes to your user account" (and to another account) */
inline QWidget *buildAccountPage(const Nav &nav, bool own)
{
    const Account a = own ? self() : targetAccount();
    if (!own && isSelf(a)) {
        return buildAccountPage(nav, true);
    }
    QWidget *side = nullptr;
    auto *content = new QWidget;
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(26, 22, 30, 20);
    v->setSpacing(14);
    v->addWidget(title(own ? tr7("Make changes to your user account") : tr7("Make changes to %1's account").arg(a.name())));
    auto *row = new QHBoxLayout;
    row->setSpacing(40);
    auto *links = new QVBoxLayout;
    links->setSpacing(6);
    auto go = [nav, a](int view) {
        return [nav, a, view] {
            target() = a.path;
            nav.open(view);
        };
    };
    if (own) {
        if (a.hasPassword()) {
            links->addWidget(shieldLink(tr7("Change your password"), go(PasswordView), false));
            links->addWidget(shieldLink(tr7("Remove your password"), go(RemovePasswordView), false));
        } else {
            links->addWidget(shieldLink(tr7("Create a password for your account"), go(PasswordView), false));
        }
        links->addWidget(shieldLink(tr7("Change your picture"), go(PictureView), false));
        links->addWidget(shieldLink(tr7("Change your account name"), go(NameView)));
        links->addWidget(shieldLink(tr7("Change your account type"), go(TypeView)));
        links->addSpacing(14);
        links->addWidget(shieldLink(tr7("Manage another account"), [nav] { nav.open(ManageView); }));
        side = Win7Display::sidePane(nav.home,
                                     {{tr7("Manage another account", "user tasks"), [nav] { nav.open(ManageView); }},
                                      {tr7("Change my environment variables"), [content] { (new EnvironmentDialog(content->window()))->show(); }}},
                                     {});
    } else {
        links->addWidget(shieldLink(tr7("Change the account name"), go(NameView), false));
        if (a.hasPassword()) {
            links->addWidget(shieldLink(tr7("Change the password"), go(PasswordView), false));
            links->addWidget(shieldLink(tr7("Remove the password"), go(RemovePasswordView), false));
        } else {
            links->addWidget(shieldLink(tr7("Create a password"), go(PasswordView), false));
        }
        links->addWidget(shieldLink(tr7("Change the picture"), go(PictureView), false));
        links->addWidget(shieldLink(tr7("Change the account type"), go(TypeView), false));
        links->addWidget(shieldLink(tr7("Delete the account"), go(DeleteView), false));
        links->addSpacing(14);
        links->addWidget(shieldLink(tr7("Manage another account"), [nav] { nav.open(ManageView); }, false));
    }
    links->addStretch(1);
    row->addLayout(links);
    auto *card = new AccountTile(a);
    card->setFixedWidth(254);
    row->addWidget(card, 0, Qt::AlignTop);
    row->addStretch(1);
    v->addLayout(row);
    v->addStretch(1);
    if (!side) {
        auto t = taskPage(false);
        t.body->addWidget(content);
        content->layout()->setContentsMargins(0, 0, 0, 0);
        return t.root;
    }
    return Win7Display::pageWithSide(side, content);
}

/** "Choose the account you would like to change" */
inline QWidget *buildManagePage(const Nav &nav)
{
    auto t = taskPage(false);
    t.body->addWidget(title(tr7("Choose the account you would like to change")));
    t.body->addSpacing(8);
    auto *box = new QFrame;
    box->setObjectName(QStringLiteral("win7accbox"));
    box->setStyleSheet(QStringLiteral("#win7accbox { background: #ffffff; border: 1px solid #ccd5e0; }"));
    box->setMinimumHeight(290);
    auto *grid = new QGridLayout(box);
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setSpacing(10);
    QList<Account> list = accounts();
    const QString guest = findUser(QStringLiteral("guest"));
    Account g;
    if (!guest.isEmpty()) {
        g = account(guest);
    } else {
        g.user = QStringLiteral("guest");
        g.real = tr7("Guest");
        g.locked = true;
    }
    g.real = tr7("Guest");
    list.append(g);
    for (int i = 0; i < list.size(); ++i) {
        const Account a = list.at(i);
        auto *tile = new AccountTile(a, [nav, a] {
            target() = a.path;
            nav.open(a.isGuest() ? GuestView : AccountView);
        });
        tile->setFixedWidth(300);
        grid->addWidget(tile, i / 2, i % 2, Qt::AlignTop | Qt::AlignLeft);
    }
    grid->setRowStretch((list.size() + 1) / 2, 1);
    grid->setColumnStretch(2, 1);
    t.body->addWidget(box);
    t.body->addSpacing(6);
    t.body->addWidget(new Link(tr7("Create an account"), [nav] { nav.open(CreateView); }));
    t.body->addWidget(new Link(tr7("What is a user account?"), [box] {
        info(box->window(), tr7("User Accounts"),
             tr7("A user account is a collection of information that tells Windows which files and folders a person can access, what changes they can make to the computer, and their personal preferences (desktop background, screen saver and so on).\n\nThanks to user accounts several people can share one computer, and each has their own files and settings."));
    }));
    t.body->addSpacing(14);
    auto *more = new QLabel(tr7("Additional things you can do"));
    QFont bf = more->font();
    bf.setBold(true);
    more->setFont(bf);
    t.body->addWidget(more);
    t.body->addWidget(new Link(tr7("Go to the main User Accounts page"), [nav] { nav.open(HomeView); }));
    return t.root;
}

/** "Name the account and choose an account type" */
inline QWidget *buildCreatePage(const Nav &nav)
{
    auto t = taskPage();
    t.body->addWidget(title(tr7("Name the account and choose an account type")));
    t.body->addSpacing(6);
    t.body->addWidget(text(tr7("This name will appear on the Welcome screen and on the Start menu.")));
    auto *name = field(tr7("New account name"));
    t.body->addWidget(name);
    t.body->addSpacing(8);
    QButtonGroup *type = typeChoice(t.body, 0);
    t.body->addSpacing(8);
    t.body->addWidget(new Link(tr7("Why is a standard account recommended?"), [name] { typeHelp(name->window()); }));
    auto *create = button(t.buttons, tr7("Create an account"));
    auto *cancel = button(t.buttons, tr7("Cancel"));
    create->setEnabled(false);
    QObject::connect(name, &QLineEdit::textChanged, create, [create](const QString &s) { create->setEnabled(!s.trimmed().isEmpty()); });
    QObject::connect(cancel, &QPushButton::clicked, t.root, [nav] { nav.back(); });
    QObject::connect(create, &QPushButton::clicked, t.root, [nav, name, type] {
        const QString shown = name->text().trimmed();
        QDBusMessage m = QDBusMessage::createMethodCall(kService, QStringLiteral("/org/freedesktop/Accounts"), kService, QStringLiteral("CreateUser"));
        m.setArguments({loginName(shown), shown, type->checkedId()});
        m.setInteractiveAuthorizationAllowed(true);
        const QDBusMessage r = QDBusConnection::systemBus().call(m, QDBus::BlockWithGui, 300000);
        if (r.type() == QDBusMessage::ErrorMessage) {
            done(name->window(), r.errorName().contains(QLatin1String("PermissionDenied")) ? QStringLiteral("cancelled") : r.errorMessage());
            return;
        }
        // a new account gets the Windows 7 gerbera; its owner sets a password later ("Create a password")
        const Account fresh = account(r.arguments().value(0).value<QDBusObjectPath>().path());
        if (fresh.valid()) {
            userCall(fresh, QStringLiteral("SetIconFile"), {picturesDir() + QStringLiteral("/user.png")});
            userCall(fresh, QStringLiteral("SetPasswordMode"), {2});
        }
        nav.open(ManageView);
    });
    return t.root;
}

/** "Create / change a password" for your own account or another one */
inline QWidget *buildPasswordPage(const Nav &nav)
{
    const Account a = targetAccount();
    const bool own = isSelf(a);
    const bool change = a.hasPassword();
    auto t = taskPage();
    QString heading;
    if (own) {
        heading = change ? tr7("Change your password") : tr7("Create a password for your account");
    } else {
        heading = change ? tr7("Change %1's password").arg(a.name()) : tr7("Create a password for %1's account").arg(a.name());
    }
    t.body->addWidget(title(heading));
    t.body->addWidget(accountHead(a), 0, Qt::AlignLeft);
    if (!own) {
        auto *warn = text(tr7("If you do this, %1 will lose all saved passwords (KWallet) for websites and network resources.\n\nTo avoid this, ask the user to change the password themselves.")
                              .arg(a.name()));
        t.body->addWidget(warn);
        t.body->addSpacing(6);
    }
    QLineEdit *current = nullptr;
    if (own && change) {
        current = field(tr7("Current password"), true);
        t.body->addWidget(current);
    }
    auto *fresh = field(tr7("New password"), true);
    auto *again = field(tr7("Confirm new password"), true);
    t.body->addWidget(fresh);
    t.body->addWidget(again);
    t.body->addWidget(text(tr7("If the password contains capital letters, they must be typed the same way every time you log on.")));
    t.body->addWidget(new Link(tr7("How to create a strong password"), [fresh] { strongPasswordHelp(fresh->window()); }));
    t.body->addSpacing(10);
    auto *hint = field(tr7("Type a password hint"));
    hint->setText(a.hint);
    t.body->addWidget(hint);
    t.body->addWidget(text(tr7("The password hint will be visible to everyone who uses this computer.")));
    t.body->addWidget(new Link(tr7("What is a password hint?"), [hint] { hintHelp(hint->window()); }));
    auto *ok = button(t.buttons, change ? tr7("Change password") : tr7("Create password"));
    auto *cancel = button(t.buttons, tr7("Cancel"));
    QObject::connect(cancel, &QPushButton::clicked, t.root, [nav] { nav.back(); });
    QObject::connect(ok, &QPushButton::clicked, t.root, [nav, a, own, current, fresh, again, hint] {
        QWidget *w = fresh->window();
        if (fresh->text() != again->text()) {
            QMessageBox::warning(w, tr7("User Accounts"), tr7("The new passwords do not match. Type them again."));
            again->clear();
            again->setFocus();
            return;
        }
        if (fresh->text().isEmpty()) {
            QMessageBox::warning(w, tr7("User Accounts"), tr7("Type a new password."));
            return;
        }
        if (!hint->text().isEmpty() && hint->text().contains(fresh->text())) {
            QMessageBox::warning(w, tr7("User Accounts"), tr7("The password hint cannot contain the password."));
            return;
        }
        if (own) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const QString err = changeOwnPassword(current ? current->text() : QString(), fresh->text());
            QApplication::restoreOverrideCursor();
            if (!err.isEmpty()) {
                QMessageBox::warning(w, tr7("User Accounts"), err);
                return;
            }
            userCall(a, QStringLiteral("SetPasswordHint"), {hint->text()});
        } else if (!done(w, userCall(a, QStringLiteral("SetPassword"), {cryptPassword(fresh->text()), hint->text()}))) {
            return;
        }
        dropImplicitAutologin(w, a);
        nav.back();
    });
    return t.root;
}

/** "Remove your password" */
inline QWidget *buildRemovePasswordPage(const Nav &nav)
{
    const Account a = targetAccount();
    const bool own = isSelf(a);
    auto t = taskPage();
    t.body->addWidget(title(own ? tr7("Remove your password") : tr7("Remove %1's password").arg(a.name())));
    t.body->addWidget(accountHead(a), 0, Qt::AlignLeft);
    t.body->addSpacing(6);
    t.body->addWidget(text(own ? tr7("Are you sure you want to remove your password?") : tr7("Are you sure you want to remove %1's password?").arg(a.name())));
    auto *warn = text(tr7("Without a password, anyone can log on to this account%1.")
                          .arg(a.type == 1 ? tr7(" and approve system changes (installing programs and so on) without a password") : QString()));
    warn->setStyleSheet(QStringLiteral("color: #555555;"));
    t.body->addWidget(warn);
    QLineEdit *current = nullptr;
    if (own) {
        t.body->addSpacing(6);
        current = field(tr7("Current password"), true);
        current->setFixedWidth(250);
        t.body->addWidget(current);
    }
    auto *ok = button(t.buttons, tr7("Remove Password"));
    auto *cancel = button(t.buttons, tr7("Cancel"));
    QObject::connect(cancel, &QPushButton::clicked, t.root, [nav] { nav.back(); });
    QObject::connect(ok, &QPushButton::clicked, t.root, [nav, a, current, ok] {
        if (current && !checkOwnPassword(current->text())) {
            QMessageBox::warning(ok->window(), tr7("User Accounts"), tr7("The current password is not correct."));
            current->clear();
            return;
        }
        // Windows 7: the only account without a password logs on by itself; both changes under one prompt
        const QList<Account> all = accounts();
        if (all.size() == 1 && all.first().uid == a.uid && autologin().user.isEmpty()) {
            if (setAutologin(ok->window(), a.user, true, a.user)) {
                userCall(a, QStringLiteral("SetPasswordHint"), {QString()});
                nav.back();
            }
            return;
        }
        if (done(ok->window(), userCall(a, QStringLiteral("SetPasswordMode"), {2}))) {
            userCall(a, QStringLiteral("SetPasswordHint"), {QString()});
            nav.back();
        }
    });
    return t.root;
}

/** "Type a new account name" */
inline QWidget *buildNamePage(const Nav &nav)
{
    const Account a = targetAccount();
    auto t = taskPage();
    t.body->addWidget(title(isSelf(a) ? tr7("Type a new account name") : tr7("Type a new account name for %1's account").arg(a.name())));
    t.body->addWidget(accountHead(a), 0, Qt::AlignLeft);
    auto *name = field(tr7("New account name"));
    t.body->addWidget(name);
    t.body->addWidget(text(tr7("This name will appear on the Welcome screen and on the Start menu.")));
    auto *ok = button(t.buttons, tr7("Rename"));
    auto *cancel = button(t.buttons, tr7("Cancel"));
    ok->setEnabled(false);
    QObject::connect(name, &QLineEdit::textChanged, ok, [ok](const QString &s) { ok->setEnabled(!s.trimmed().isEmpty()); });
    QObject::connect(cancel, &QPushButton::clicked, t.root, [nav] { nav.back(); });
    QObject::connect(ok, &QPushButton::clicked, t.root, [nav, a, name] {
        if (done(name->window(), userCall(a, QStringLiteral("SetRealName"), {name->text().trimmed()}))) {
            nav.back();
        }
    });
    return t.root;
}

/** "Choose a new account type" */
inline QWidget *buildTypePage(const Nav &nav)
{
    const Account a = targetAccount();
    const bool own = isSelf(a);
    auto t = taskPage();
    t.body->addWidget(title(own ? tr7("Choose a new account type") : tr7("Choose a new account type for %1").arg(a.name())));
    t.body->addWidget(accountHead(a), 0, Qt::AlignLeft);
    t.body->addSpacing(6);
    const bool lastAdmin = a.type == 1 && adminCount() <= 1;
    if (lastAdmin) {
        t.body->addWidget(text(tr7("Before you change this account type, give another user of this computer administrator rights. This way the computer always has at least one administrator account.")));
        t.body->addSpacing(4);
    }
    QButtonGroup *type = typeChoice(t.body, a.type);
    if (lastAdmin) {
        type->button(0)->setEnabled(false);
    }
    t.body->addSpacing(8);
    t.body->addWidget(new Link(tr7("Why is a standard account recommended?"), [type] { typeHelp(type->button(0)->window()); }));
    auto *ok = button(t.buttons, tr7("Change the account type"));
    auto *cancel = button(t.buttons, tr7("Cancel"));
    ok->setEnabled(false);
    QObject::connect(type, &QButtonGroup::idClicked, ok, [ok, a](int id) { ok->setEnabled(id != a.type); });
    QObject::connect(cancel, &QPushButton::clicked, t.root, [nav] { nav.back(); });
    QObject::connect(ok, &QPushButton::clicked, t.root, [nav, a, type, ok] {
        if (done(ok->window(), userCall(a, QStringLiteral("SetAccountType"), {type->checkedId()}))) {
            nav.back();
        }
    });
    return t.root;
}

/** one picture of the grid; the chosen one gets the blue selection box */
class PictureChoice : public QWidget
{
public:
    PictureChoice(const QString &file, std::function<void(const QString &)> picked)
        : m_file(file)
        , m_picked(std::move(picked))
    {
        setFixedSize(62, 62);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        m_pm = framedPicture(QPixmap(file), 48);
    }
    bool selected = false;
    QString file() const
    {
        return m_file;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (selected || underMouse()) {
            QLinearGradient g(0, 0, 0, height());
            g.setColorAt(0, selected ? QColor(0xe8, 0xf3, 0xfd) : QColor(0xf5, 0xfa, 0xfe));
            g.setColorAt(1, selected ? QColor(0xc4, 0xdf, 0xfb) : QColor(0xe3, 0xef, 0xfd));
            p.setBrush(g);
            p.setPen(selected ? QColor(0x7d, 0xa2, 0xce) : QColor(0xb8, 0xd6, 0xfb));
            p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        }
        p.drawPixmap((width() - m_pm.width()) / 2, (height() - m_pm.height()) / 2, m_pm);
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
        if (e->button() == Qt::LeftButton) {
            m_picked(m_file);
        }
    }

private:
    QString m_file;
    QPixmap m_pm;
    std::function<void(const QString &)> m_picked;
};

/** "Choose a new picture for your account" */
inline QWidget *buildPicturePage(const Nav &nav)
{
    const Account a = targetAccount();
    auto t = taskPage();
    t.body->addWidget(title(isSelf(a) ? tr7("Choose a new picture for your account")
                                       : tr7("Choose a new picture for %1's account").arg(a.name())));
    t.body->addWidget(accountHead(a), 0, Qt::AlignLeft);
    t.body->addWidget(text(tr7("The picture you choose will appear on the Welcome screen and on the Start menu.")));
    auto *box = new QFrame;
    box->setObjectName(QStringLiteral("win7picbox"));
    box->setStyleSheet(QStringLiteral("#win7picbox { background: #ffffff; border: 1px solid #ccd5e0; }"));
    auto *grid = new QGridLayout(box);
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setSpacing(4);
    t.body->addWidget(box, 0, Qt::AlignLeft);
    auto chosen = std::make_shared<QString>();
    auto *ok = button(t.buttons, tr7("Change Picture"));
    auto *cancel = button(t.buttons, tr7("Cancel"));
    ok->setEnabled(false);
    auto tiles = std::make_shared<QList<PictureChoice *>>();
    auto pick = [chosen, tiles, ok](const QString &f) {
        *chosen = f;
        for (PictureChoice *c : *tiles) {
            c->selected = c->file() == f;
            c->update();
        }
        ok->setEnabled(true);
    };
    const QStringList pics = stockPictures();
    for (int i = 0; i < pics.size(); ++i) {
        auto *c = new PictureChoice(pics.at(i), pick);
        tiles->append(c);
        grid->addWidget(c, i / 9, i % 9);
    }
    t.body->addWidget(new Link(tr7("Browse for more pictures..."), [box, a, nav] {
        const QString f = QFileDialog::getOpenFileName(box->window(), tr7("Open"),
                                                       QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                                                       tr7("Image files (*.bmp *.gif *.jpg *.jpeg *.png *.dib *.rle *.webp)"));
        if (!f.isEmpty() && setPicture(box->window(), a, f)) {
            nav.back();
        }
    }));
    QObject::connect(cancel, &QPushButton::clicked, t.root, [nav] { nav.back(); });
    QObject::connect(ok, &QPushButton::clicked, t.root, [nav, a, chosen, ok] {
        if (!chosen->isEmpty() && setPicture(ok->window(), a, *chosen)) {
            nav.back();
        }
    });
    return t.root;
}

/** "Do you want to keep X's files?" then "Are you sure you want to delete X's account?" */
inline QWidget *buildDeletePage(const Nav &nav)
{
    const Account a = targetAccount();
    auto t = taskPage();
    auto *heading = title(tr7("Do you want to keep %1's files?").arg(a.name()));
    t.body->addWidget(heading);
    t.body->addWidget(accountHead(a), 0, Qt::AlignLeft);
    auto *explain = text(tr7("Before you delete %1's account, Windows can automatically save the contents of %1's home folder (desktop, Documents, Music, Pictures, Videos and so on) to a new folder called '%1' on your desktop. However, Windows cannot save %1's e-mail messages and other settings.")
                             .arg(a.name()));
    t.body->addWidget(explain);
    auto *delFiles = button(t.buttons, tr7("Delete Files"));
    auto *keepFiles = button(t.buttons, tr7("Keep Files"));
    auto *confirm = button(t.buttons, tr7("Delete Account"));
    auto *cancel = button(t.buttons, tr7("Cancel"));
    confirm->hide();
    auto keep = std::make_shared<bool>(false);
    auto toConfirm = [=](bool keepThem) {
        *keep = keepThem;
        heading->setText(tr7("Are you sure you want to delete %1's account?").arg(a.name()));
        explain->setText(keepThem ? tr7("Windows will save the files of %1 to a folder called '%1' on your desktop, and then delete the %1 account.").arg(a.name())
                                  : tr7("Windows will delete all of %1's files, and then delete %1's account.").arg(a.name()));
        delFiles->hide();
        keepFiles->hide();
        confirm->show();
    };
    QObject::connect(delFiles, &QPushButton::clicked, t.root, [toConfirm] { toConfirm(false); });
    QObject::connect(keepFiles, &QPushButton::clicked, t.root, [toConfirm] { toConfirm(true); });
    QObject::connect(cancel, &QPushButton::clicked, t.root, [nav] { nav.back(); });
    QObject::connect(confirm, &QPushButton::clicked, t.root, [nav, a, keep, confirm] {
        QWidget *w = confirm->window();
        if (isSelf(a)) {
            return;
        }
        if (*keep) {
            const QString home = QStringLiteral("/home/") + a.user; // AccountsService keeps homes there
            const QString dest = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + QLatin1Char('/') + a.name();
            const int code = QProcess::execute(QStringLiteral("pkexec"),
                                               {QStringLiteral("/bin/sh"), QStringLiteral("-c"),
                                                QStringLiteral("mkdir -p \"$2\" && cp -a \"$1\"/. \"$2\"/ && chown -R \"$3\" \"$2\""), QStringLiteral("sh"), home, dest,
                                                QString::number(getuid()) + QLatin1Char(':') + QString::number(getgid())});
            if (code != 0) {
                if (code != 126 && code != 127) {
                    QMessageBox::warning(w, tr7("User Accounts"), tr7("The files could not be saved. The account was not deleted."));
                }
                return;
            }
        }
        if (done(w, call(QStringLiteral("/org/freedesktop/Accounts"), kService, QStringLiteral("DeleteUser"), {a.uid, true}))) {
            nav.open(ManageView);
        }
    });
    return t.root;
}

/** the Guest account: "Do you want to turn on the guest account?" or its own small page */
inline QWidget *buildGuestPage(const Nav &nav)
{
    const QString path = findUser(QStringLiteral("guest"));
    Account g = path.isEmpty() ? Account() : account(path);
    const bool on = g.valid() && !g.locked;
    if (!g.valid()) {
        g.user = QStringLiteral("guest");
        g.locked = true;
    }
    g.real = tr7("Guest");
    auto t = taskPage(!on);
    if (on) {
        t.body->addWidget(title(tr7("Make changes to the guest account")));
        auto *row = new QHBoxLayout;
        row->setSpacing(40);
        auto *links = new QVBoxLayout;
        links->setSpacing(6);
        links->addWidget(shieldLink(tr7("Change the picture"), [nav, g] {
            target() = g.path;
            nav.open(PictureView);
        }, false));
        links->addWidget(shieldLink(tr7("Turn off the guest account"), [nav, g] {
            if (done(nullptr, userCall(g, QStringLiteral("SetLocked"), {true}))) {
                nav.open(ManageView);
            }
        }, false));
        links->addStretch(1);
        row->addLayout(links);
        auto *card = new AccountTile(g);
        card->setFixedWidth(254);
        row->addWidget(card, 0, Qt::AlignTop);
        row->addStretch(1);
        t.body->addLayout(row);
        return t.root;
    }
    t.body->addWidget(title(tr7("Do you want to turn on the guest account?")));
    t.body->addWidget(accountHead(g), 0, Qt::AlignLeft);
    t.body->addWidget(text(tr7("If you turn on the guest account, people who do not have an account can use the guest account to log on to the computer. Password-protected files, folders, or settings are not accessible to guest users.")));
    auto *ok = button(t.buttons, tr7("Turn on"));
    auto *cancel = button(t.buttons, tr7("Cancel"));
    QObject::connect(cancel, &QPushButton::clicked, t.root, [nav] { nav.back(); });
    QObject::connect(ok, &QPushButton::clicked, t.root, [nav, path, ok] {
        QWidget *w = ok->window();
        Account guest;
        if (path.isEmpty()) {
            QDBusMessage m = QDBusMessage::createMethodCall(kService, QStringLiteral("/org/freedesktop/Accounts"), kService, QStringLiteral("CreateUser"));
            m.setArguments({QStringLiteral("guest"), tr7("Guest"), 0});
            m.setInteractiveAuthorizationAllowed(true);
            const QDBusMessage r = QDBusConnection::systemBus().call(m, QDBus::BlockWithGui, 300000);
            if (r.type() == QDBusMessage::ErrorMessage) {
                done(w, r.errorName().contains(QLatin1String("PermissionDenied")) ? QStringLiteral("cancelled") : r.errorMessage());
                return;
            }
            guest = account(r.arguments().value(0).value<QDBusObjectPath>().path());
            userCall(guest, QStringLiteral("SetIconFile"), {picturesDir() + QStringLiteral("/guest.png")});
            userCall(guest, QStringLiteral("SetPasswordMode"), {2});
        } else {
            guest = account(path);
        }
        if (done(w, userCall(guest, QStringLiteral("SetLocked"), {false}))) {
            nav.open(ManageView);
        }
    });
    return t.root;
}

inline QWidget *build(int view, const Nav &nav)
{
    switch (view) {
    case HomeView:
        target().clear();
        return buildAccountPage(nav, true);
    case ManageView:
        return buildManagePage(nav);
    case CreateView:
        return buildCreatePage(nav);
    case AccountView:
        return buildAccountPage(nav, false);
    case PasswordView:
        return buildPasswordPage(nav);
    case RemovePasswordView:
        return buildRemovePasswordPage(nav);
    case NameView:
        return buildNamePage(nav);
    case TypeView:
        return buildTypePage(nav);
    case PictureView:
        return buildPicturePage(nav);
    case DeleteView:
        return buildDeletePage(nav);
    case GuestView:
        return buildGuestPage(nav);
    }
    return new QWidget;
}

/** title bar / address bar names of the pages */
inline QString viewTitle(int view)
{
    switch (view) {
    case HomeView:
        return tr7("User Accounts");
    case ManageView:
        return tr7("Manage Accounts");
    case CreateView:
        return tr7("Create New Account");
    case AccountView:
    case GuestView:
        return tr7("Change an Account");
    case PasswordView:
        return tr7("Change the password");
    case RemovePasswordView:
        return tr7("Remove the password");
    case NameView:
        return tr7("Change Name");
    case TypeView:
        return tr7("Change the account type");
    case PictureView:
        return tr7("Choose Picture");
    case DeleteView:
        return tr7("Delete the account");
    }
    return QString();
}

inline bool isUsersView(int v)
{
    return v >= GuestView && v <= DeleteView;
}

/* ---------------- netplwiz / control userpasswords2: the "User Accounts" dialog ----------------
 * Users tab as in Windows 7 (reference ~/VMs/sevenplasma/refs/netplwiz_users.png): "Users must enter a user name and
 * password to use this computer" = no SDDM automatic logon; list with groups; Add / Remove / Properties;
 * the password box of the selected user with Reset Password (other users only). Icons netplwiz.dll 102 / 108. */

class AutoLogonDialog : public QDialog
{
public:
    QLineEdit *name, *pw, *again;
    AutoLogonDialog(const QString &user, QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(tr7("Automatically Log On"));
        auto *v = new QVBoxLayout(this);
        auto *t = new QLabel(tr7("You can set up your computer so that users do not have to type a user name and password to log on. To do this, specify a user that will be automatically logged on below:"));
        t->setWordWrap(true);
        v->addWidget(t);
        auto *g = new QGridLayout;
        name = new QLineEdit(user);
        pw = new QLineEdit;
        again = new QLineEdit;
        pw->setEchoMode(QLineEdit::Password);
        again->setEchoMode(QLineEdit::Password);
        g->addWidget(new QLabel(tr7("User name:")), 0, 0);
        g->addWidget(name, 0, 1);
        g->addWidget(new QLabel(tr7("Password:")), 1, 0);
        g->addWidget(pw, 1, 1);
        g->addWidget(new QLabel(tr7("Confirm Password:")), 2, 0);
        g->addWidget(again, 2, 1);
        v->addLayout(g);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("OK"));
        bb->button(QDialogButtonBox::Cancel)->setText(tr7("Cancel"));
        connect(bb, &QDialogButtonBox::accepted, this, [this] {
            if (pw->text() != again->text()) {
                QMessageBox::warning(this, windowTitle(), tr7("The passwords do not match."));
                return;
            }
            if (findUser(name->text().trimmed()).isEmpty()) {
                QMessageBox::warning(this, windowTitle(), tr7("The user \"%1\" was not found.").arg(name->text().trimmed()));
                return;
            }
            accept();
        });
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        v->addWidget(bb);
        resize(380, sizeHint().height());
    }
};

class UserPropertiesDialog : public QDialog
{
public:
    UserPropertiesDialog(const Account &a, QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(tr7("%1 Properties").arg(a.user));
        auto *v = new QVBoxLayout(this);
        auto *tabs = new QTabWidget;
        auto *gen = new QWidget;
        auto *g = new QGridLayout(gen);
        auto *user = new QLineEdit(a.user);
        user->setReadOnly(true);
        auto *full = new QLineEdit(a.real);
        g->addWidget(new QLabel(tr7("User name:")), 0, 0);
        g->addWidget(user, 0, 1);
        g->addWidget(new QLabel(tr7("Full Name:")), 1, 0);
        g->addWidget(full, 1, 1);
        g->setRowStretch(2, 1);
        tabs->addTab(gen, tr7("General"));
        auto *grp = new QWidget;
        auto *gv = new QVBoxLayout(grp);
        gv->addWidget(new QLabel(tr7("What level of access do you want to grant this user?")));
        auto *std_ = new QRadioButton(tr7("Standard user", "netplwiz"));
        auto *adm = new QRadioButton(tr7("Administrator"));
        auto *d1 = new QLabel(tr7("Standard account users can use most software and can change system settings that do not affect other users."));
        auto *d2 = new QLabel(tr7("Administrators have complete access to the computer and can make any changes."));
        for (QLabel *l : {d1, d2}) {
            l->setWordWrap(true);
            l->setContentsMargins(20, 0, 0, 6);
        }
        gv->addWidget(std_);
        gv->addWidget(d1);
        gv->addWidget(adm);
        gv->addWidget(d2);
        gv->addStretch(1);
        (a.type == 1 ? adm : std_)->setChecked(true);
        if (isSelf(a)) {
            // as in Windows 7 an administrator does not demote himself here
            std_->setEnabled(false);
        }
        tabs->addTab(grp, tr7("Group Membership"));
        v->addWidget(tabs);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("OK"));
        bb->button(QDialogButtonBox::Cancel)->setText(tr7("Cancel"));
        connect(bb, &QDialogButtonBox::accepted, this, [this, a, full, adm] {
            if (full->text() != a.real && !Win7Users::done(this, userCall(a, QStringLiteral("SetRealName"), {full->text()}))) {
                return;
            }
            const int type = adm->isChecked() ? 1 : 0;
            if (type != a.type && !Win7Users::done(this, userCall(a, QStringLiteral("SetAccountType"), {type}))) {
                return;
            }
            accept();
        });
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        v->addWidget(bb);
        resize(400, 330);
    }
};

class NetplwizDialog : public QDialog
{
public:
    explicit NetplwizDialog(QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(tr7("User Accounts"));
        auto *v = new QVBoxLayout(this);
        auto *tabs = new QTabWidget;
        auto *page = new QWidget;
        auto *pv = new QVBoxLayout(page);
        auto *top = new QHBoxLayout;
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(QStringLiteral("seven-netplwiz-users")).pixmap(32, 32));
        top->addWidget(ic, 0, Qt::AlignTop);
        auto *intro = new QLabel(tr7("Use the list below to grant or deny users access to your computer, and to change passwords and other settings."));
        intro->setWordWrap(true);
        top->addWidget(intro, 1);
        pv->addLayout(top);
        must = new QCheckBox(tr7("Users must enter a user name and password to use this computer"));
        pv->addSpacing(4);
        pv->addWidget(must);
        pv->addWidget(new QLabel(tr7("Users for this computer:")));
        list = new QTreeWidget;
        list->setHeaderLabels({tr7("User Name"), tr7("Group")});
        list->setRootIsDecorated(false);
        list->header()->resizeSection(0, 190);
        list->setMinimumHeight(130);
        pv->addWidget(list);
        auto *bh = new QHBoxLayout;
        bh->addStretch(1);
        add = new QPushButton(tr7("Add..."));
        remove = new QPushButton(tr7("Delete"));
        props = new QPushButton(tr7("Properties"));
        for (QPushButton *b : {add, remove, props}) {
            bh->addWidget(b);
        }
        pv->addLayout(bh);
        box = new QGroupBox;
        auto *gh = new QHBoxLayout(box);
        auto *pic = new QLabel;
        pic->setPixmap(QIcon::fromTheme(QStringLiteral("seven-netplwiz-user")).pixmap(32, 32));
        gh->addWidget(pic, 0, Qt::AlignTop);
        auto *gv = new QVBoxLayout;
        boxText = new QLabel;
        boxText->setWordWrap(true);
        gv->addWidget(boxText);
        auto *rh = new QHBoxLayout;
        rh->addStretch(1);
        reset = new QPushButton(tr7("Reset Password..."));
        rh->addWidget(reset);
        gv->addLayout(rh);
        gh->addLayout(gv, 1);
        pv->addWidget(box);
        tabs->addTab(page, tr7("Users"));
        v->addWidget(tabs);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
        bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("OK"));
        bb->button(QDialogButtonBox::Cancel)->setText(tr7("Cancel"));
        bb->button(QDialogButtonBox::Apply)->setText(tr7("Apply"));
        apply = bb->button(QDialogButtonBox::Apply);
        v->addWidget(bb);

        connect(bb, &QDialogButtonBox::accepted, this, [this] {
            if (save()) {
                accept();
            }
        });
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(apply, &QPushButton::clicked, this, [this] { save(); });
        connect(must, &QCheckBox::toggled, this, [this] { changed(); });
        connect(list, &QTreeWidget::currentItemChanged, this, [this] { selection(); });
        connect(add, &QPushButton::clicked, this, [] {
            // account creation lives in Control Panel > User Accounts > Manage Accounts
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--manage-users")});
        });
        connect(remove, &QPushButton::clicked, this, [this] { removeUser(); });
        connect(props, &QPushButton::clicked, this, [this] {
            UserPropertiesDialog d(current(), this);
            if (d.exec() == QDialog::Accepted) {
                load();
            }
        });
        connect(reset, &QPushButton::clicked, this, [this] { resetPassword(); });
        load();
        resize(470, 520);
    }

private:
    QCheckBox *must;
    QTreeWidget *list;
    QPushButton *add, *remove, *props, *reset, *apply;
    QGroupBox *box;
    QLabel *boxText;
    QList<Account> m_accounts;
    bool m_savedMust = true;

    Account current() const
    {
        const int i = list->currentItem() ? list->indexOfTopLevelItem(list->currentItem()) : -1;
        return m_accounts.value(i);
    }
    void load()
    {
        const QString keep = current().user;
        m_accounts = accounts();
        list->clear();
        for (const Account &a : std::as_const(m_accounts)) {
            auto *it = new QTreeWidgetItem(list, {a.user, a.type == 1 ? tr7("Administrators; Users") : tr7("Users")});
            it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-netplwiz-user")));
            if (a.user == keep || (keep.isEmpty() && isSelf(a))) {
                list->setCurrentItem(it);
            }
        }
        if (!list->currentItem() && list->topLevelItemCount()) {
            list->setCurrentItem(list->topLevelItem(0));
        }
        m_savedMust = autologin().user.isEmpty();
        QSignalBlocker b(must);
        must->setChecked(m_savedMust);
        changed();
        selection();
    }
    void changed()
    {
        apply->setEnabled(must->isChecked() != m_savedMust);
    }
    void selection()
    {
        const Account a = current();
        const bool me = isSelf(a);
        remove->setEnabled(a.valid() && !me);
        props->setEnabled(a.valid());
        box->setTitle(tr7("Password for %1").arg(a.user));
        boxText->setText(me ? tr7("To change your password, open User Accounts in Control Panel and select Change your password.")
                            : tr7("To change the password for %1, click Reset Password.").arg(a.user));
        reset->setEnabled(a.valid() && !me);
    }
    bool save()
    {
        if (must->isChecked() == m_savedMust) {
            return true;
        }
        if (must->isChecked()) {
            if (!setAutologin(this, QString(), false)) {
                return false;
            }
        } else {
            AutoLogonDialog d(current().valid() ? current().user : self().user, this);
            if (d.exec() != QDialog::Accepted || !setAutologin(this, d.name->text().trimmed(), false)) {
                return false;
            }
        }
        load();
        return true;
    }
    void removeUser()
    {
        const Account a = current();
        if (QMessageBox::warning(this, windowTitle(), tr7("Are you sure you want to remove %1?\n\nTheir files in the home folder stay on the disk.").arg(a.user),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes) {
            return;
        }
        if (Win7Users::done(this, call(QStringLiteral("/org/freedesktop/Accounts"), kService, QStringLiteral("DeleteUser"), {a.uid, false}))) {
            if (autologin().user == a.user) {
                setAutologin(this, QString(), false);
            }
            load();
        }
    }
    void resetPassword()
    {
        const Account a = current();
        QDialog d(this);
        d.setWindowTitle(tr7("Reset Password"));
        auto *v = new QVBoxLayout(&d);
        auto *t = new QLabel(tr7("Type a new password for %1:").arg(a.user));
        v->addWidget(t);
        auto *g = new QGridLayout;
        auto *pw = new QLineEdit;
        auto *again = new QLineEdit;
        pw->setEchoMode(QLineEdit::Password);
        again->setEchoMode(QLineEdit::Password);
        g->addWidget(new QLabel(tr7("New password:")), 0, 0);
        g->addWidget(pw, 0, 1);
        g->addWidget(new QLabel(tr7("Confirm new password:")), 1, 0);
        g->addWidget(again, 1, 1);
        v->addLayout(g);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("OK"));
        bb->button(QDialogButtonBox::Cancel)->setText(tr7("Cancel"));
        v->addWidget(bb);
        connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        connect(bb, &QDialogButtonBox::accepted, &d, [&] {
            if (pw->text() != again->text()) {
                QMessageBox::warning(&d, d.windowTitle(), tr7("The passwords do not match."));
                return;
            }
            const QString err = pw->text().isEmpty() ? userCall(a, QStringLiteral("SetPasswordMode"), {2})
                                                     : userCall(a, QStringLiteral("SetPassword"), {cryptPassword(pw->text()), QString()});
            if (Win7Users::done(&d, err)) {
                if (!pw->text().isEmpty()) {
                    dropImplicitAutologin(&d, a);
                }
                d.accept();
            }
        });
        d.exec();
    }
};

} // namespace Win7Users
