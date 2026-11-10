#pragma once
#include "win7i18n.h"
/*
 * Devices for the Burn and Sync tabs: optical burners (Solid OpticalDrive; data discs written with xorriso) and
 * sync targets (removable storage mounted by Solid, phones / players over MTP through KIO's mtp:/).
 */
#include <KIO/CopyJob>
#include <KIO/JobUiDelegateFactory>
#include <KIO/ListJob>
#include <KJobWidgets>
#include <Solid/Block>
#include <Solid/Device>
#include <Solid/DeviceNotifier>
#include <Solid/OpticalDisc>
#include <Solid/OpticalDrive>
#include <Solid/PortableMediaPlayer>
#include <Solid/StorageAccess>
#include <Solid/StorageDrive>

#include <QFileInfo>
#include <QProcess>
#include <QStorageInfo>

struct SyncTarget {
    QString name;
    QUrl folder; // where synced files go
    qint64 free = -1;
    qint64 total = -1;
    bool mtp = false;
};

namespace Devices
{
/** the first optical drive that can write, with its block device */
inline QPair<QString, QString> burner()
{
    for (const Solid::Device &d : Solid::Device::listFromType(Solid::DeviceInterface::OpticalDrive)) {
        const auto *drive = d.as<Solid::OpticalDrive>();
        if (drive && (drive->supportedMedia() & (Solid::OpticalDrive::Cdr | Solid::OpticalDrive::Cdrw | Solid::OpticalDrive::Dvdr | Solid::OpticalDrive::Dvdrw
                                                 | Solid::OpticalDrive::Dvdplusr | Solid::OpticalDrive::Dvdplusrw))) {
            const auto *block = d.as<Solid::Block>();
            const QString dev = block ? block->device() : QStringLiteral("/dev/sr0");
            const auto media = drive->supportedMedia();
            const QString kind = (media & (Solid::OpticalDrive::Bd | Solid::OpticalDrive::Bdr | Solid::OpticalDrive::Bdre)) ? QStringLiteral("BD-RE")
                : (media & (Solid::OpticalDrive::Dvdrw | Solid::OpticalDrive::Dvdplusrw))                                 ? QStringLiteral("DVD RW")
                : (media & (Solid::OpticalDrive::Dvdr | Solid::OpticalDrive::Dvdplusr))                                   ? QStringLiteral("DVD-R")
                                                                                                                            : QStringLiteral("CD-RW");
            // the Windows name: "Дисковод DVD RW (E:)"; the device name stands in for the letter
            return {tr7("Drive %1 (%2)").arg(kind, QFileInfo(dev).fileName()), dev};
        }
    }
    return {};
}

/** removable mounted storage (USB flash, card readers) */
inline QList<SyncTarget> storageTargets()
{
    QList<SyncTarget> out;
    for (const Solid::Device &d : Solid::Device::listFromType(Solid::DeviceInterface::StorageAccess)) {
        const auto *acc = d.as<Solid::StorageAccess>();
        if (!acc || !acc->isAccessible() || acc->filePath().isEmpty()) {
            continue;
        }
        // walk up to the drive to see whether it is removable
        Solid::Device p = d;
        bool removable = false;
        while (p.isValid()) {
            if (const auto *drv = p.as<Solid::StorageDrive>()) {
                removable = drv->isRemovable() || drv->isHotpluggable();
                break;
            }
            p = p.parent();
        }
        // never the system: only volumes mounted by the user session (/run/media, /media) on a drive that
        // does not hold / or /boot (the system disk here is itself a USB drive)
        const QString mp = acc->filePath();
        if (!removable || !(mp.startsWith(QLatin1String("/run/media/")) || mp.startsWith(QLatin1String("/media/")))) {
            continue;
        }
        SyncTarget t;
        t.name = d.description().isEmpty() ? QFileInfo(acc->filePath()).fileName() : d.description();
        const QString music = acc->filePath() + QStringLiteral("/Music");
        t.folder = QUrl::fromLocalFile(music);
        QStorageInfo si(acc->filePath());
        t.free = si.bytesAvailable();
        t.total = si.bytesTotal();
        out << t;
    }
    return out;
}

/** MTP players / phones known to Solid */
inline QStringList mtpPlayers()
{
    QStringList out;
    for (const Solid::Device &d : Solid::Device::listFromType(Solid::DeviceInterface::PortableMediaPlayer)) {
        const auto *pmp = d.as<Solid::PortableMediaPlayer>();
        if (pmp && pmp->supportedProtocols().contains(QLatin1String("mtp"))) {
            out << (d.product().isEmpty() ? d.description() : d.product());
        }
    }
    return out;
}
}
