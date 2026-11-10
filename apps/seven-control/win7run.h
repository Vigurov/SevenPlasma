/*
 * The Run dialog of Windows 7 (shell32.dll dialog 1003): icon 160, the history in a drop-down box, OK, Cancel
 * and Browse. It understands what one types into Windows' Run box (control, cmd, notepad, taskmgr, *.msc,
 * *.cpl, %VARIABLES%) and maps it to the Windows 7 look-alikes of this desktop; anything else is a Linux
 * command, a path or an address. History in seven-runrc, like the RunMRU of the registry.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "win7folderopts.h"

#include <QComboBox>
#include <QCompleter>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QCoreApplication>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace Win7Run
{

using Win7FolderOpts::dlu;

inline QSettings &store()
{
    static QSettings s(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/seven-runrc"), QSettings::IniFormat);
    return s;
}

/** %TEMP%, %USERPROFILE% and the other variables of Windows, then the ones of Linux */
inline QString expandVariables(QString text)
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString home = QDir::homePath();
    static const QRegularExpression re(QStringLiteral("%([A-Za-z_][A-Za-z0-9_()]*)%"));
    QRegularExpressionMatch m;
    int from = 0;
    while ((m = re.match(text, from)).hasMatch()) {
        const QString name = m.captured(1).toUpper();
        QString value;
        if (name == QLatin1String("TEMP") || name == QLatin1String("TMP")) {
            value = QDir::tempPath();
        } else if (name == QLatin1String("USERPROFILE") || name == QLatin1String("HOMEPATH")) {
            value = home;
        } else if (name == QLatin1String("APPDATA")) {
            value = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        } else if (name == QLatin1String("LOCALAPPDATA")) {
            value = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        } else if (name == QLatin1String("WINDIR") || name == QLatin1String("SYSTEMROOT")) {
            value = QStringLiteral("/usr");
        } else if (name == QLatin1String("PROGRAMFILES") || name == QLatin1String("PROGRAMFILES(X86)")) {
            value = QStringLiteral("/usr/bin");
        } else if (name == QLatin1String("USERNAME")) {
            value = env.value(QStringLiteral("USER"));
        } else {
            value = env.value(m.captured(1), env.value(name));
        }
        text.replace(m.capturedStart(), m.capturedLength(), value);
        from = m.capturedStart() + value.size();
    }
    if (text.startsWith(QLatin1Char('~'))) {
        text.replace(0, 1, home);
    }
    return text;
}

/** What Windows' Run box knows, as the programs of this desktop */
inline QStringList windowsCommand(const QString &word, const QStringList &args)
{
    const QString self = QCoreApplication::applicationFilePath();
    QString w = word.toLower();
    if (w.endsWith(QLatin1String(".exe"))) {
        w.chop(4);
    }
    static const QHash<QString, QStringList> map{
        {QStringLiteral("control"), {}},
        {QStringLiteral("control.exe"), {}},
        {QStringLiteral("devmgmt.msc"), {QStringLiteral("seven-devmgr")}},
        {QStringLiteral("hdwwiz.cpl"), {QStringLiteral("seven-devmgr")}},
        {QStringLiteral("compmgmt.msc"), {QStringLiteral("--compmgmt")}},
        {QStringLiteral("diskmgmt.msc"), {QStringLiteral("--diskmgmt")}},
        {QStringLiteral("eventvwr"), {QStringLiteral("--eventvwr")}},
        {QStringLiteral("eventvwr.msc"), {QStringLiteral("--eventvwr")}},
        {QStringLiteral("wf.msc"), {QStringLiteral("--wf")}},
        {QStringLiteral("firewall.cpl"), {QStringLiteral("--firewall")}},
        {QStringLiteral("appwiz.cpl"), {QStringLiteral("--programs")}},
        {QStringLiteral("ncpa.cpl"), {QStringLiteral("--net-connections")}},
        {QStringLiteral("desk.cpl"), {QStringLiteral("--resolution")}},
        {QStringLiteral("main.cpl"), {QStringLiteral("--mouse")}},
        {QStringLiteral("mmsys.cpl"), {QStringLiteral("--sound")}},
        {QStringLiteral("timedate.cpl"), {QStringLiteral("--datetime")}},
        {QStringLiteral("intl.cpl"), {QStringLiteral("--region")}},
        {QStringLiteral("powercfg.cpl"), {QStringLiteral("--power")}},
        {QStringLiteral("wscui.cpl"), {QStringLiteral("--action-center")}},
        {QStringLiteral("wuapp"), {QStringLiteral("--windows-update")}},
        {QStringLiteral("charmap"), {QStringLiteral("--charmap")}},
        {QStringLiteral("inetcpl.cpl"), {QStringLiteral("--inetopts")}},
        {QStringLiteral("colorcpl"), {QStringLiteral("--colormgmt")}},
        {QStringLiteral("cleanmgr"), {QStringLiteral("--cleanmgr")}},
        {QStringLiteral("cttune"), {QStringLiteral("--cttune")}},
        {QStringLiteral("osk"), {QStringLiteral("--osk")}},
        {QStringLiteral("magnify"), {QStringLiteral("--magnifier")}},
        {QStringLiteral("narrator"), {QStringLiteral("--narrator")}},
        {QStringLiteral("rstrui"), {QStringLiteral("--rstrui")}},
        {QStringLiteral("sdclt"), {QStringLiteral("--backup")}},
        {QStringLiteral("notepad"), {QStringLiteral("--notepad")}},
        {QStringLiteral("fonts"), {QStringLiteral("--fonts")}},
        {QStringLiteral("netplwiz"), {QStringLiteral("--netplwiz")}},
        {QStringLiteral("control userpasswords2"), {QStringLiteral("--netplwiz")}},
        {QStringLiteral("taskmgr"), {QStringLiteral("seven-taskmgr")}},
        {QStringLiteral("resmon"), {QStringLiteral("seven-taskmgr"), QStringLiteral("--resmon")}},
        {QStringLiteral("perfmon /res"), {QStringLiteral("seven-taskmgr"), QStringLiteral("--resmon")}},
        {QStringLiteral("sysdm.cpl"), {QStringLiteral("seven-system")}},
        {QStringLiteral("msinfo32"), {QStringLiteral("seven-system")}},
        {QStringLiteral("cmd"), {QStringLiteral("konsole")}},
        {QStringLiteral("powershell"), {QStringLiteral("konsole")}},
        {QStringLiteral("explorer"), {QStringLiteral("dolphin")}},
        {QStringLiteral("calc"), {QStringLiteral("seven-calc")}},
        {QStringLiteral("snippingtool"), {QStringLiteral("spectacle")}},
        {QStringLiteral("mspaint"), {QStringLiteral("seven-paint")}},
        {QStringLiteral("pbrush"), {QStringLiteral("seven-paint")}},
        {QStringLiteral("write"), {QStringLiteral("--notepad")}},
        {QStringLiteral("wordpad"), {QStringLiteral("--notepad")}},
    };
    if (!map.contains(w)) {
        return {};
    }
    const QStringList target = map.value(w);
    // a flag of seven-control, or another program
    if (target.isEmpty() || target.first().startsWith(QLatin1String("--"))) {
        return QStringList{self} + target + args;
    }
    return target + args;
}

inline bool isUrl(const QString &text)
{
    return text.contains(QLatin1String("://")) || text.startsWith(QLatin1String("www.")) || text.startsWith(QLatin1String("mailto:"));
}

/** Runs what was typed; false when it could not be found */
inline bool execute(const QString &typed)
{
    const QString text = expandVariables(typed.trimmed());
    if (text.isEmpty()) {
        return false;
    }
    if (isUrl(text)) {
        return QProcess::startDetached(QStringLiteral("xdg-open"), {text.startsWith(QLatin1String("www.")) ? QStringLiteral("https://") + text : text});
    }
    // a folder or a document: opened by its program, a folder in the Explorer
    if (QFileInfo::exists(text) && !QFileInfo(text).isExecutable()) {
        return QProcess::startDetached(QStringLiteral("xdg-open"), {text});
    }
    if (QFileInfo(text).isDir()) {
        return QProcess::startDetached(QStringLiteral("dolphin"), {text});
    }
    QStringList words = QProcess::splitCommand(text);
    if (words.isEmpty()) {
        return false;
    }
    // "mmc devmgmt.msc" and "control appwiz.cpl" open the snap-in or the applet named
    if ((words.first().toLower() == QLatin1String("mmc") || words.first().toLower() == QLatin1String("control")) && words.size() > 1) {
        words.removeFirst();
    }
    QString first = words.takeFirst();
    QStringList mapped = windowsCommand(first, words);
    if (!mapped.isEmpty()) {
        const QString program = mapped.takeFirst();
        if (QStandardPaths::findExecutable(program).isEmpty() && !QFileInfo(program).isExecutable()) {
            return false;
        }
        return QProcess::startDetached(program, mapped);
    }
    const QString found = QFileInfo(first).isExecutable() ? first : QStandardPaths::findExecutable(first);
    if (found.isEmpty()) {
        // a desktop file name, as the Start menu knows the programs
        const QString desktop = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, first + QStringLiteral(".desktop"));
        if (!desktop.isEmpty()) {
            return QProcess::startDetached(QStringLiteral("gtk-launch"), {first});
        }
        return false;
    }
    return QProcess::startDetached(found, words);
}

/** what the drop-down offers before anything has been run, and what the completer knows */
inline QStringList commonCommands()
{
    return {QStringLiteral("control"), QStringLiteral("cmd"), QStringLiteral("notepad"), QStringLiteral("calc"), QStringLiteral("explorer"),
            QStringLiteral("taskmgr"), QStringLiteral("devmgmt.msc"), QStringLiteral("appwiz.cpl"), QStringLiteral("msinfo32"),
            QStringLiteral("resmon"), QStringLiteral("mspaint"), QStringLiteral("compmgmt.msc"), QStringLiteral("diskmgmt.msc"),
            QStringLiteral("eventvwr"), QStringLiteral("sysdm.cpl"), QStringLiteral("ncpa.cpl"), QStringLiteral("mmsys.cpl"),
            QStringLiteral("powercfg.cpl"), QStringLiteral("charmap"), QStringLiteral("cleanmgr"), QStringLiteral("snippingtool")};
}

class RunDialog : public QDialog
{
public:
    RunDialog()
    {
        setWindowTitle(QStringLiteral("Виконати"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-run")));
        setAttribute(Qt::WA_DeleteOnClose);
        setFixedSize(dlu(0, 0, 227, 92).size());
        auto *icon = new QLabel(this);
        icon->setGeometry(dlu(7, 11, 21, 20));
        icon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-run")).pixmap(32, 32));
        auto *text = new QLabel(QStringLiteral("Введіть ім'я програми, папки, документа або ресурсу Інтернету, і Windows відкриє їх."), this);
        text->setGeometry(dlu(36, 11, 184, 22));
        text->setWordWrap(true);
        text->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        auto *label = new QLabel(QStringLiteral("Відкрити:"), this);
        label->setGeometry(dlu(7, 39, 33, 10));
        m_combo = new QComboBox(this);
        m_combo->setGeometry(dlu(40, 37, 179, 12).adjusted(0, -2, 0, 2));
        m_combo->setEditable(true);
        m_combo->setInsertPolicy(QComboBox::NoInsert);
        // the drop-down arrow opens the history (RunMRU); until something has been run it offers the usual commands
        const QStringList history = store().value(QStringLiteral("History")).toStringList();
        m_combo->addItems(history.isEmpty() ? commonCommands() : history);
        m_combo->setMaxVisibleItems(12);
        QStringList known = history + commonCommands();
        known.removeDuplicates();
        auto *completer = new QCompleter(known, m_combo);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setCompletionMode(QCompleter::PopupCompletion);
        m_combo->setCompleter(completer);
        if (history.isEmpty()) {
            m_combo->setCurrentIndex(-1);
            m_combo->setEditText(QString());
        } else {
            m_combo->setCurrentIndex(0);
        }
        m_combo->lineEdit()->selectAll();
        label->setBuddy(m_combo);
        m_ok = new QPushButton(QStringLiteral("OK"), this);
        m_ok->setGeometry(dlu(62, 70, 50, 14));
        m_ok->setDefault(true);
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"), this);
        cancel->setGeometry(dlu(116, 70, 50, 14));
        auto *browse = new QPushButton(QStringLiteral("Огляд..."), this);
        browse->setGeometry(dlu(170, 70, 50, 14));
        m_ok->setEnabled(!m_combo->currentText().trimmed().isEmpty());
        connect(m_combo, &QComboBox::editTextChanged, this, [this](const QString &t) { m_ok->setEnabled(!t.trimmed().isEmpty()); });
        connect(cancel, &QPushButton::clicked, this, &QDialog::close);
        connect(m_ok, &QPushButton::clicked, this, [this] { run(); });
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Огляд"), QStringLiteral("/usr/bin"), QStringLiteral("Програми (*);;Усі файли (*)"));
            if (!f.isEmpty()) {
                m_combo->setEditText(f.contains(QLatin1Char(' ')) ? QLatin1Char('"') + f + QLatin1Char('"') : f);
                m_combo->setFocus();
            }
        });
        m_combo->setFocus();
        placeAboveStart();
    }

protected:
    void showEvent(QShowEvent *e) override
    {
        QDialog::showEvent(e);
        // the frame size is known only once the window manager has decorated the window
        QTimer::singleShot(0, this, [this] { placeAboveStart(); });
    }

private:
    /** Windows 7 opens Run in the bottom-left corner of the work area, just above the Start button */
    void placeAboveStart()
    {
        QScreen *scr = QGuiApplication::screenAt(QCursor::pos());
        if (!scr) {
            scr = QGuiApplication::primaryScreen();
        }
        const QRect avail = scr->availableGeometry();
        // move() places the outer frame; before the first show only the client size is known
        const int frameH = isVisible() ? frameGeometry().height() : height() + 31;
        move(avail.left() + 12, avail.bottom() + 1 - frameH - 12);
    }

    void run()
    {
        const QString typed = m_combo->currentText().trimmed();
        if (!execute(typed)) {
            QMessageBox box(this);
            box.setIcon(QMessageBox::Critical);
            box.setWindowTitle(typed);
            box.setText(QStringLiteral("Не вдається знайти «%1». Перевірте правильність імені й повторіть спробу.").arg(typed));
            box.exec();
            m_combo->lineEdit()->selectAll();
            return;
        }
        // the most recent first, 26 at most like the RunMRU list
        QStringList history = store().value(QStringLiteral("History")).toStringList();
        history.removeAll(typed);
        history.prepend(typed);
        store().setValue(QStringLiteral("History"), history.mid(0, 26));
        store().sync();
        close();
    }

    QComboBox *m_combo = nullptr;
    QPushButton *m_ok = nullptr;
};

} // namespace Win7Run
