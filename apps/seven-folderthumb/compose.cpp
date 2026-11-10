/*
 * Windows 7 style folder thumbnails, measured on a real Windows 7 (Explorer, 256 px icons, controlled test
 * pictures): the first two files of the folder stand in it — the first one upright at the back, the second one
 * turned towards the viewer in front of it — each shown whole with a thin white frame; files that are not
 * pictures stand as a white card with their icon. A folder holding only folders gets Windows' own
 * "folder with folders" icon (imageres.dll).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "compose.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QImageReader>
#include <QLocale>
#include <QMimeDatabase>
#include <QPainter>
#include <QPolygonF>
#include <QSettings>
#include <QStandardPaths>
#include <QTransform>
#include <cmath>

namespace Win7Folder
{

/* The folder pictures are Windows 7's own (imageres.dll), so they are not part of the source: install.sh extracts
   them from the user's Windows 7 ISO into <data>/sevenplasma/folderthumb/ (see ASSETS.txt for the list). */
static QString asset(const QString &name)
{
    static QHash<QString, QString> cache;
    auto it = cache.constFind(name);
    if (it != cache.constEnd()) {
        return *it;
    }
    const QString path = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("sevenplasma/folderthumb/") + name);
    cache.insert(name, path);
    return path;
}

static QImage part(const QString &name, int size)
{
    for (int s : {16, 32, 48, 256}) {
        if (s >= size || s == 256) {
            QImage img(asset(QStringLiteral("%1-%2.png").arg(name).arg(s)));
            if (img.width() != size) {
                img = img.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
            return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }
    }
    return {};
}

static void listDir(const QString &dir, QStringList *files, QStringList *dirs)
{
    QDirIterator it(dir, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
    int seen = 0;
    while (it.hasNext() && seen < 2000) {
        it.next();
        ++seen;
        const QFileInfo fi = it.fileInfo();
        if (fi.isHidden() || fi.fileName().startsWith(QLatin1Char('.')) || fi.fileName().endsWith(QLatin1String(".directory"))) {
            continue;
        }
        (fi.isDir() ? dirs : files)->append(fi.filePath());
    }
    QCollator c(QLocale(QLocale::English)); // like Windows: Latin names before Cyrillic ones
    c.setNumericMode(true);
    c.setCaseSensitivity(Qt::CaseInsensitive);
    auto byName = [&c](const QString &a, const QString &b) {
        return c.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
    };
    std::sort(files->begin(), files->end(), byName);
    std::sort(dirs->begin(), dirs->end(), byName);
}

QStringList previewFiles(const QString &dir, int max)
{
    // Windows 7 takes the first files by name, whatever they are (subfolders are not shown when there are files)
    QStringList files, dirs;
    listDir(dir, &files, &dirs);
    return files.mid(0, qMin(max, 2));
}

QStringList otherFiles(const QString &dir, int max)
{
    // only consulted when there are no files: the subfolders
    QStringList files, dirs;
    listDir(dir, &files, &dirs);
    return files.isEmpty() ? dirs.mid(0, max) : QStringList();
}

static bool isPicture(const QString &file)
{
    static const QMimeDatabase db;
    const QFileInfo fi(file);
    return fi.size() > 0 && fi.size() < 60 * 1024 * 1024
        && db.mimeTypeForFile(fi, QMimeDatabase::MatchExtension).name().startsWith(QLatin1String("image/"))
        && QImageReader::supportedImageFormats().contains(QImageReader::imageFormat(file));
}

static QImage cropTransparent(const QImage &src)
{
    int l = src.width(), t = src.height(), r = -1, b = -1;
    for (int y = 0; y < src.height(); ++y) {
        for (int x = 0; x < src.width(); ++x) {
            if (qAlpha(src.pixel(x, y)) > 24) {
                l = qMin(l, x), r = qMax(r, x), t = qMin(t, y), b = qMax(b, y);
            }
        }
    }
    return r >= l ? src.copy(l, t, r - l + 1, b - t + 1) : src;
}

/** the item as Windows shows it in a folder: the picture itself, or a white card with the file's icon */
static QImage card(const QString &file, int px)
{
    QImage img;
    if (isPicture(file)) {
        QImageReader r(file);
        r.setAutoTransform(true);
        const QSize s = r.size();
        if (s.isValid()) {
            r.setScaledSize(s.scaled(QSize(px, px), Qt::KeepAspectRatio));
        }
        img = r.read();
        // very long or tall pictures are cut (in the middle) to at most 2:1
        if (!img.isNull() && img.width() > 2 * img.height()) {
            const int w = 2 * img.height();
            img = img.copy((img.width() - w) / 2, 0, w, img.height());
        } else if (!img.isNull() && img.height() > 2 * img.width()) {
            const int h = 2 * img.width();
            img = img.copy(0, (img.height() - h) / 2, img.width(), h);
        }
    }
    const int frame = qMax(1, qRound(px / 100.0));
    if (img.isNull()) {
        // white square card with the 256 px icon of the file
        static const QMimeDatabase db;
        if (QIcon::themeName().isEmpty() || QIcon::themeName() == QLatin1String("hicolor")) {
            const QSettings kde(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kdeglobals"), QSettings::IniFormat);
            QIcon::setThemeName(kde.value(QStringLiteral("Icons/Theme"), QStringLiteral("breeze")).toString());
            QIcon::setFallbackThemeName(QStringLiteral("breeze"));
        }
        const QMimeType mt = db.mimeTypeForFile(file);
        QImage icon;
        if (mt.inherits(QStringLiteral("text/plain"))) {
            icon = cropTransparent(QImage(asset(QStringLiteral("page-text.png"))));
        } else {
            QIcon ic = QIcon::fromTheme(mt.iconName());
            if (ic.isNull()) {
                ic = QIcon::fromTheme(mt.genericIconName(), QIcon::fromTheme(QStringLiteral("text-x-generic")));
            }
            icon = cropTransparent(ic.pixmap(QSize(256, 256)).toImage());
        }
        img = QImage(px, px, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        if (!icon.isNull()) {
            const QImage s = icon.scaled(px * 74 / 100, px * 80 / 100, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QPainter p(&img);
            p.drawImage((px - s.width()) / 2 - px / 30, (px - s.height()) / 2 + px / 30, s);
        }
    }
    // a light grey outline (2 px at 256) right around the picture
    QImage out(img.width() + 2 * frame, img.height() + 2 * frame, QImage::Format_ARGB32_Premultiplied);
    out.fill(QColor(0xcc, 0xcc, 0xcc));
    QPainter p(&out);
    p.drawImage(frame, frame, img);
    p.end();
    out.setText(QStringLiteral("seven-file-card"), isPicture(file) ? QString() : QStringLiteral("1"));
    return out;
}

/** size on screen (256 px icon) of an item standing in the folder, measured for w/h = 2, 1, 1/2 */
static QSizeF standSize(qreal aspect)
{
    const qreal l = std::log2(qBound(0.125, aspect, 8.0)); // 1 .. -1 between the measured ones
    auto lerp = [](qreal a, qreal b, qreal t) {
        return a + (b - a) * t;
    };
    if (l >= 0) {
        const qreal t = qMin(l, 1.0);
        qreal w = lerp(128, 124, t), h = lerp(175, 135, t);
        if (l > 1) {
            h = 135 * std::pow(2.0, -(l - 1) * 0.6); // even wider: lower still
        }
        return {w, qMax(h, 60.0)};
    }
    const qreal t = qMin(-l, 1.0);
    qreal w = lerp(128, 76, t), h = lerp(175, 183, t);
    if (-l > 1) {
        w = 76 * std::pow(2.0, -(-l - 1)); // even taller: narrower
    }
    return {qMax(w, 24.0), h};
}

QImage compose(const QStringList &files, int size, qreal dpr, const QStringList &iconFiles)
{
    const int px = qRound(size * dpr);
    const qreal k = px / 256.0;
    if (files.isEmpty()) {
        if (iconFiles.isEmpty()) {
            return {};
        }
        // only subfolders: Windows' own icon of a folder holding folders
        QImage img = QImage(asset(QStringLiteral("folder-folders.png"))).scaled(px, px, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        img.setDevicePixelRatio(dpr);
        return img;
    }

    QList<QImage> cards;
    for (const QString &f : files) {
        const QImage c = card(f, qMax(64, qRound(200 * k)));
        if (!c.isNull()) {
            cards << c;
        }
    }
    if (cards.isEmpty()) {
        return {};
    }

    auto drawIn = [](QPainter &p, const QImage &img, const QPolygonF &q) {
        QTransform t;
        const QRectF r(0, 0, img.width(), img.height());
        if (!QTransform::quadToQuad(QPolygonF({r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()}), q, t)) {
            return;
        }
        p.save();
        p.setTransform(t);
        p.drawImage(r, img);
        p.restore();
    };
    auto pt = [k](qreal x, qreal y) {
        return QPointF(x * k, y * k);
    };

    QImage out(px, px, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    p.drawImage(0, 0, part(QStringLiteral("back"), px));

    // the first item stands at the back: upright, its right edge and foot fixed, sheared down to the right
    {
        const QImage &c = cards.at(0);
        const qreal aspect = qreal(c.width()) / c.height();
        const bool fileCard = !c.text(QStringLiteral("seven-file-card")).isEmpty();
        // a file's white card stands a little bigger and further right than a picture
        const QSizeF s = fileCard ? QSizeF(142, 178) : standSize(aspect);
        // with an item in front, a tall picture moves aside to the right
        const qreal shift = cards.size() > 1 ? 15 * qBound(0.0, (1 - aspect) / 0.5, 1.0) : 0;
        const qreal xr = (fileCard ? 200 : 190) + shift, xl = xr - s.width();
        auto bottom = [](qreal x) {
            return 206 + 0.235 * (x - 60);
        };
        drawIn(p, c, QPolygonF({pt(xl, bottom(xl) - s.height()), pt(xr, bottom(xr) - s.height()), pt(xr, bottom(xr)), pt(xl, bottom(xl))}));
    }
    // the second one in front, turned towards the viewer (narrow, steeper), a little shaded on its left
    if (cards.size() > 1) {
        const QImage &c = cards.at(1);
        const qreal aspect = qreal(c.width()) / c.height();
        const QSizeF s = standSize(aspect);
        const qreal xl = 70 + 12 * qBound(0.0, aspect - 1, 1.0), xr = xl + 48;
        auto bottom = [](qreal x) {
            return 221 + 0.477 * (x - 70);
        };
        const QPolygonF q({pt(xl, bottom(xl) - s.height()), pt(xr, bottom(xr) - s.height()), pt(xr, bottom(xr)), pt(xl, bottom(xl))});
        QImage shaded = c.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        {
            QPainter sp(&shaded);
            QLinearGradient g(0, 0, shaded.width(), 0);
            g.setColorAt(0, QColor(0, 0, 0, 90));
            g.setColorAt(0.35, QColor(0, 0, 0, 0));
            sp.setCompositionMode(QPainter::CompositionMode_SourceAtop);
            sp.fillRect(shaded.rect(), g);
        }
        drawIn(p, shaded, q);
    }

    p.drawImage(0, 0, part(QStringLiteral("front"), px));
    p.end();
    out.setDevicePixelRatio(dpr);
    return out;
}
}
