#pragma once
#include <QImage>
#include <QStringList>

namespace Win7Folder
{
/** Up to three image files of a folder that make a good preview (sorted by name) */
QStringList previewFiles(const QString &dir, int max = 3);
/** Up to three other files (documents, music...) shown by their file-type icon when there are no pictures */
QStringList otherFiles(const QString &dir, int max = 3);
/** Windows 7 style folder: back panel, the pictures standing inside, front flap; when there are no pictures
 *  the icons of iconFiles stand there instead (like Explorer's document sheets); empty image if neither */
QImage compose(const QStringList &files, int size, qreal dpr = 1.0, const QStringList &iconFiles = {});
}
