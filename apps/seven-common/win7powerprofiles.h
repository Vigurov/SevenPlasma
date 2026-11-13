/*
 * Power profiles (power saver / balanced / performance) over D-Bus: org.freedesktop.UPower.PowerProfiles is
 * offered by power-profiles-daemon (Ubuntu, Arch) and by tuned-ppd (Fedora, which has no powerprofilesctl).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDBusArgument>
#include <QStringList>
#include <QVariantMap>

namespace Win7PowerProfiles
{
inline const QString service = QStringLiteral("org.freedesktop.UPower.PowerProfiles");
inline const QString path = QStringLiteral("/org/freedesktop/UPower/PowerProfiles");

inline QVariant property(const QString &name)
{
    QDBusMessage m = QDBusMessage::createMethodCall(service, path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    m << service << name;
    const QDBusMessage r = QDBusConnection::systemBus().call(m, QDBus::Block, 3000);
    if (r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty()) {
        return {};
    }
    return r.arguments().first().value<QDBusVariant>().variant();
}

/** the active profile ("balanced"...), empty without a profiles daemon */
inline QString active()
{
    return property(QStringLiteral("ActiveProfile")).toString();
}

/** the profiles the daemon offers, in its order */
inline QStringList available()
{
    QStringList out;
    const QVariant v = property(QStringLiteral("Profiles"));
    if (!v.canConvert<QDBusArgument>()) {
        return out;
    }
    const QDBusArgument arg = v.value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd()) {
        QVariantMap map;
        arg >> map;
        out << map.value(QStringLiteral("Profile")).toString();
    }
    arg.endArray();
    return out;
}

inline bool setActive(const QString &profile)
{
    QDBusMessage m = QDBusMessage::createMethodCall(service, path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"));
    m << service << QStringLiteral("ActiveProfile") << QVariant::fromValue(QDBusVariant(profile));
    return QDBusConnection::systemBus().call(m, QDBus::Block, 8000).type() == QDBusMessage::ReplyMessage;
}
}
