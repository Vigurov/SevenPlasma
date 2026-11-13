/*
 * "System" (Win+Pause, Computer > Properties, sysdm.cpl): in Windows 7 a page of the Control Panel's window,
 * so this becomes seven-control on it (exec: the same process, so nothing is left behind when it was started
 * as a service or a scope).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <QByteArray>
#include <QDir>
#include <QFile>

#include <unistd.h>

int main(int, char **)
{
    const QByteArray control = QFile::encodeName(QDir::homePath() + QStringLiteral("/.local/bin/seven-control"));
    if (QFile::exists(QString::fromLocal8Bit(control))) {
        execl(control.constData(), control.constData(), "--system", static_cast<char *>(nullptr));
    }
    execlp("seven-control", "seven-control", "--system", static_cast<char *>(nullptr));
    execlp("kinfocenter", "kinfocenter", static_cast<char *>(nullptr));
    return 1;
}
