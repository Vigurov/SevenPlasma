/*
 * KIO thumbnail plugin: Windows 7 style previews for folders.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "compose.h"

#include <KIO/ThumbnailCreator>
#include <KPluginFactory>

class Win7FolderCreator : public KIO::ThumbnailCreator
{
    Q_OBJECT
public:
    Win7FolderCreator(QObject *parent, const QVariantList &args)
        : KIO::ThumbnailCreator(parent, args)
    {
    }

    KIO::ThumbnailResult create(const KIO::ThumbnailRequest &request) override
    {
        if (!request.url().isLocalFile()) {
            return KIO::ThumbnailResult::fail();
        }
        const QStringList files = Win7Folder::previewFiles(request.url().toLocalFile());
        const QSize ts = request.targetSize();
        const int size = qMin(ts.width(), ts.height());
        const QStringList others = files.isEmpty() ? Win7Folder::otherFiles(request.url().toLocalFile()) : QStringList();
        const QImage img = Win7Folder::compose(files, size, request.devicePixelRatio(), others);
        if (img.isNull()) {
            return KIO::ThumbnailResult::fail();
        }
        return KIO::ThumbnailResult::pass(img);
    }
};

K_PLUGIN_CLASS_WITH_JSON(Win7FolderCreator, "sevenfolderthumbnail.json")

#include "sevenfolderthumbnail.moc"
