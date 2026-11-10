#pragma once
/*
 * "seven-control --portal": a backend of xdg-desktop-portal for the Windows 7 dialogs. Programs that open files
 * through the portal (Chrome, Telegram, Flatpak and Snap programs...) get our "Open with" dialog instead of KDE's
 * "Choose application". Only org.freedesktop.impl.portal.AppChooser is implemented; everything else stays with the
 * KDE backend (portals.conf: org.freedesktop.impl.portal.AppChooser=win7).
 *
 *   D-Bus name   org.freedesktop.impl.portal.desktop.win7, object /org/freedesktop/portal/desktop
 *   ChooseApplication(o handle, s app_id, s parent_window, as choices, a{sv} options) -> (u response, a{sv} results)
 *     results: choice = desktop file id without ".desktop"; response 0 = chosen, 1 = cancelled
 */
#include "win7i18n.h"
#include "win7openwith.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVirtualObject>
#include <QWindow>

namespace Win7Portal
{
class AppChooser : public QDBusVirtualObject
{
public:
    QString introspect(const QString &path) const override
    {
        if (path != QLatin1String("/org/freedesktop/portal/desktop")) {
            return QString();
        }
        return QStringLiteral(
            "<interface name=\"org.freedesktop.impl.portal.AppChooser\">"
            "<method name=\"ChooseApplication\"><arg type=\"o\" name=\"handle\" direction=\"in\"/>"
            "<arg type=\"s\" name=\"app_id\" direction=\"in\"/><arg type=\"s\" name=\"parent_window\" direction=\"in\"/>"
            "<arg type=\"as\" name=\"choices\" direction=\"in\"/><arg type=\"a{sv}\" name=\"options\" direction=\"in\"/>"
            "<arg type=\"u\" name=\"response\" direction=\"out\"/><arg type=\"a{sv}\" name=\"results\" direction=\"out\"/></method>"
            "<method name=\"UpdateChoices\"><arg type=\"o\" name=\"handle\" direction=\"in\"/>"
            "<arg type=\"as\" name=\"choices\" direction=\"in\"/></method>"
            "<property name=\"version\" type=\"u\" access=\"read\"/></interface>");
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.interface() == QLatin1String("org.freedesktop.DBus.Properties")) {
            connection.send(message.createReply(QVariant::fromValue(QDBusVariant(uint(4)))));
            return true;
        }
        if (message.interface() != QLatin1String("org.freedesktop.impl.portal.AppChooser")) {
            return false;
        }
        if (message.member() == QLatin1String("UpdateChoices")) {
            connection.send(message.createReply());
            return true;
        }
        if (message.member() != QLatin1String("ChooseApplication") || message.arguments().size() < 5) {
            return false;
        }
        const QString parent = message.arguments().at(2).toString();
        const QVariantMap options = qdbus_cast<QVariantMap>(message.arguments().at(4));
        QString mime = options.value(QStringLiteral("content_type")).toString();
        QString name = options.value(QStringLiteral("filename")).toString();
        const QUrl uri(options.value(QStringLiteral("uri")).toString());
        if (name.isEmpty()) {
            name = uri.isLocalFile() ? QFileInfo(uri.toLocalFile()).fileName() : uri.toDisplayString();
        }
        QStringList files;
        if (uri.isLocalFile()) {
            files << uri.toLocalFile();
        } else {
            files << name;
        }
        if (mime.isEmpty()) {
            mime = QMimeDatabase().mimeTypeForFile(files.first(), QMimeDatabase::MatchExtension).name();
        }

        message.setDelayedReply(true);
        auto *dlg = new Win7OpenWith::Dialog(files, mime, name);
        auto replied = std::make_shared<bool>(false);
        const QDBusMessage msg = message;
        const QDBusConnection conn = connection;
        dlg->onChosen = [msg, conn, replied](const KService::Ptr &s) {
            QString id = s->desktopEntryName();
            if (id.isEmpty()) {
                id = s->storageId();
            }
            if (id.endsWith(QLatin1String(".desktop"))) {
                id.chop(8);
            }
            QVariantMap results{{QStringLiteral("choice"), id}};
            QDBusMessage r = msg.createReply({uint(0), results});
            QDBusConnection(conn).send(r);
            *replied = true;
        };
        QObject::connect(dlg, &QObject::destroyed, [msg, conn, replied] {
            if (!*replied) {
                QDBusConnection(conn).send(msg.createReply({uint(1), QVariantMap()}));
            }
        });
        dlg->setWindowModality(Qt::ApplicationModal);
        dlg->winId();
        if (parent.startsWith(QLatin1String("x11:"))) { // above the program that asked
            bool ok = false;
            const WId wid = parent.mid(4).toULongLong(&ok, 16);
            if (ok && dlg->windowHandle()) {
                if (QWindow *p = QWindow::fromWinId(wid)) {
                    dlg->windowHandle()->setTransientParent(p);
                }
            }
        }
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
        return true;
    }
};

/** registers the backend; false when another instance has the name */
inline bool start()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    static AppChooser chooser;
    if (!bus.registerVirtualObject(QStringLiteral("/org/freedesktop/portal/desktop"), &chooser)) {
        return false;
    }
    return bus.registerService(QStringLiteral("org.freedesktop.impl.portal.desktop.win7"));
}
}
