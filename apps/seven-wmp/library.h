#pragma once
/*
 * Media library: scans the Music / Videos / Pictures folders, reads tags with KFileMetaData and caches the
 * result in ~/.cache/seven-wmp/library.json (keyed by path + modification time).
 */
#include <KFileMetaData/EmbeddedImageData>
#include <KFileMetaData/ExtractorCollection>
#include <KFileMetaData/SimpleExtractionResult>

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFutureWatcher>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent>

struct MediaItem {
    enum Kind { Audio, Video, Picture };
    QString path;
    Kind kind = Audio;
    QString title, artist, albumArtist, album, genre, composer;
    int track = 0;
    int year = 0;
    qint64 duration = 0; // ms
    qint64 size = 0;
    qint64 mtime = 0;
    int rating = 0; // 0..5 (WMP's own)
    int width = 0, height = 0;

    QString displayTitle() const
    {
        return title.isEmpty() ? QFileInfo(path).completeBaseName() : title;
    }
    QString displayArtist() const
    {
        return artist.isEmpty() ? QStringLiteral("Невідомий виконавець") : artist;
    }
    QString displayAlbumArtist() const
    {
        return !albumArtist.isEmpty() ? albumArtist : displayArtist();
    }
    QString displayAlbum() const
    {
        return album.isEmpty() ? QStringLiteral("Невідомий альбом") : album;
    }
    QString displayGenre() const
    {
        return genre.isEmpty() ? QStringLiteral("Невідомий жанр") : genre;
    }
    QUrl url() const
    {
        return QUrl::fromLocalFile(path);
    }
};

inline QString formatLength(qint64 ms)
{
    const qint64 s = (ms + 500) / 1000;
    if (s >= 3600) {
        return QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}
/** Win7 style sizes: "4,6 МБ", "8 МБ" */
inline QString formatSize(qint64 bytes)
{
    double v = bytes;
    QStringList units{QStringLiteral("байт"), QStringLiteral("КБ"), QStringLiteral("МБ"), QStringLiteral("ГБ")};
    int u = 0;
    while (v >= 1024 && u < 3) {
        v /= 1024;
        ++u;
    }
    QString s = v >= 10 || u == 0 ? QString::number(qRound(v)) : QString::number(v, 'f', 1);
    s.replace(QLatin1Char('.'), QLatin1Char(','));
    if (s.endsWith(QStringLiteral(",0"))) {
        s.chop(2);
    }
    return s + QLatin1Char(' ') + units[u];
}

class Library : public QObject
{
    Q_OBJECT
public:
    explicit Library(QObject *parent = nullptr)
        : QObject(parent)
    {
        load();
    }

    QList<MediaItem> items(MediaItem::Kind kind) const
    {
        QList<MediaItem> out;
        for (const auto &i : m_items) {
            if (i.kind == kind) {
                out << i;
            }
        }
        return out;
    }
    const QList<MediaItem> &all() const
    {
        return m_items;
    }
    const MediaItem *find(const QString &path) const
    {
        for (const auto &i : m_items) {
            if (i.path == path) {
                return &i;
            }
        }
        return nullptr;
    }
    bool busy() const
    {
        return m_busy;
    }

    static QString kindKey(MediaItem::Kind kind)
    {
        return kind == MediaItem::Audio ? QStringLiteral("music") : kind == MediaItem::Video ? QStringLiteral("videos") : QStringLiteral("pictures");
    }
    /** the folders a library watches (Organize > Manage libraries), the standard folder by default */
    static QStringList folders(MediaItem::Kind kind)
    {
        const QStringList saved = QSettings().value(QStringLiteral("folders/") + kindKey(kind)).toStringList();
        if (!saved.isEmpty()) {
            return saved;
        }
        switch (kind) {
        case MediaItem::Audio:
            return {QStandardPaths::writableLocation(QStandardPaths::MusicLocation)};
        case MediaItem::Video:
            return {QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)};
        default:
            return {QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)};
        }
    }
    static void setFolders(MediaItem::Kind kind, const QStringList &f)
    {
        QSettings().setValue(QStringLiteral("folders/") + kindKey(kind), f);
    }

    /** album art: cover.jpg/folder.jpg next to the file, otherwise the picture embedded in the tags */
    static QImage albumArt(const QString &path)
    {
        static QHash<QString, QImage> cache;
        const QString dir = QFileInfo(path).absolutePath();
        const QString key = dir + QLatin1Char('|') + path;
        if (auto it = cache.constFind(key); it != cache.constEnd()) {
            return *it;
        }
        QImage img;
        for (const QString &n : {QStringLiteral("cover.jpg"), QStringLiteral("Cover.jpg"), QStringLiteral("folder.jpg"), QStringLiteral("Folder.jpg"),
                                 QStringLiteral("AlbumArtSmall.jpg"), QStringLiteral("cover.png"), QStringLiteral("front.jpg")}) {
            if (QFile::exists(dir + QLatin1Char('/') + n)) {
                img.load(dir + QLatin1Char('/') + n);
                if (!img.isNull()) {
                    break;
                }
            }
        }
        if (img.isNull()) {
            static KFileMetaData::ExtractorCollection extractors;
            const QString mime = QMimeDatabase().mimeTypeForFile(path).name();
            for (auto *ex : extractors.fetchExtractors(mime)) {
                KFileMetaData::SimpleExtractionResult r(path, mime, KFileMetaData::ExtractionResult::ExtractImageData);
                ex->extract(&r);
                const auto data = r.imageData();
                QByteArray bytes = data.value(KFileMetaData::EmbeddedImageData::FrontCover);
                if (bytes.isEmpty() && !data.isEmpty()) {
                    bytes = data.first();
                }
                if (!bytes.isEmpty() && img.loadFromData(bytes)) {
                    break;
                }
            }
        }
        if (!img.isNull()) {
            img = img.scaled(200, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        cache.insert(key, img);
        return img;
    }

public Q_SLOTS:
    void rescan()
    {
        if (m_busy) {
            return;
        }
        m_busy = true;
        Q_EMIT busyChanged();
        QHash<QString, MediaItem> old;
        for (const auto &i : std::as_const(m_items)) {
            old.insert(i.path, i);
        }
        auto *w = new QFutureWatcher<QList<MediaItem>>(this);
        connect(w, &QFutureWatcher<QList<MediaItem>>::progressValueChanged, this, [] {});
        connect(w, &QFutureWatcher<QList<MediaItem>>::finished, this, [this, w] {
            const auto result = w->result();
            w->deleteLater();
            // keep the user's ratings
            QHash<QString, int> ratings;
            for (const auto &i : std::as_const(m_items)) {
                if (i.rating) {
                    ratings.insert(i.path, i.rating);
                }
            }
            m_items = result;
            for (auto &i : m_items) {
                i.rating = ratings.value(i.path, i.rating);
            }
            save();
            m_busy = false;
            Q_EMIT busyChanged();
            Q_EMIT changed();
        });
        w->setFuture(QtConcurrent::run([old, this] {
            return scan(old);
        }));
    }

    /** the rating without rebuilding the views */
    void setRatingQuiet(const QString &path, int rating)
    {
        for (auto &i : m_items) {
            if (i.path == path) {
                i.rating = rating;
            }
        }
        save();
    }
    void setRating(const QString &path, int rating)
    {
        for (auto &i : m_items) {
            if (i.path == path) {
                i.rating = rating;
            }
        }
        save();
        Q_EMIT changed();
    }

Q_SIGNALS:
    void changed();
    void busyChanged();
    void scanning(const QString &folder);

private:
    QList<MediaItem> scan(const QHash<QString, MediaItem> &old)
    {
        QList<MediaItem> out;
        QMimeDatabase db;
        KFileMetaData::ExtractorCollection extractors;
        for (auto kind : {MediaItem::Audio, MediaItem::Video, MediaItem::Picture}) {
            for (const QString &folder : folders(kind)) {
                QMetaObject::invokeMethod(this, [this, folder] {
                    Q_EMIT scanning(folder);
                });
                QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
                while (it.hasNext()) {
                    const QString path = it.next();
                    const QFileInfo fi = it.fileInfo();
                    const QString mime = db.mimeTypeForFile(fi).name();
                    MediaItem::Kind k;
                    if (mime.startsWith(QLatin1String("audio/"))) {
                        k = MediaItem::Audio;
                    } else if (mime.startsWith(QLatin1String("video/"))) {
                        k = MediaItem::Video;
                    } else if (mime.startsWith(QLatin1String("image/"))) {
                        k = MediaItem::Picture;
                    } else {
                        continue;
                    }
                    const qint64 mtime = fi.lastModified().toSecsSinceEpoch();
                    if (auto o = old.constFind(path); o != old.constEnd() && o->mtime == mtime && o->size == fi.size()) {
                        out << *o;
                        continue;
                    }
                    MediaItem m;
                    m.path = path;
                    m.kind = k;
                    m.size = fi.size();
                    m.mtime = mtime;
                    if (k != MediaItem::Picture) {
                        for (auto *ex : extractors.fetchExtractors(mime)) {
                            KFileMetaData::SimpleExtractionResult r(path, mime, KFileMetaData::ExtractionResult::ExtractMetaData);
                            ex->extract(&r);
                            const auto props = r.properties();
                            auto str = [&](KFileMetaData::Property::Property p) {
                                const auto v = props.values(p);
                                QStringList l;
                                for (const auto &x : v) {
                                    l << x.toString();
                                }
                                return l.join(QStringLiteral("; "));
                            };
                            using P = KFileMetaData::Property::Property;
                            if (m.title.isEmpty()) {
                                m.title = str(P::Title);
                            }
                            if (m.artist.isEmpty()) {
                                m.artist = str(P::Artist);
                            }
                            if (m.albumArtist.isEmpty()) {
                                m.albumArtist = str(P::AlbumArtist);
                            }
                            if (m.album.isEmpty()) {
                                m.album = str(P::Album);
                            }
                            if (m.genre.isEmpty()) {
                                m.genre = str(P::Genre);
                            }
                            if (m.composer.isEmpty()) {
                                m.composer = str(P::Composer);
                            }
                            if (!m.track) {
                                m.track = props.value(P::TrackNumber).toInt();
                            }
                            if (!m.year) {
                                m.year = props.value(P::ReleaseYear).toInt();
                            }
                            if (!m.duration) {
                                m.duration = qint64(props.value(P::Duration).toDouble() * 1000);
                            }
                            if (!m.width) {
                                m.width = props.value(P::Width).toInt();
                                m.height = props.value(P::Height).toInt();
                            }
                        }
                    }
                    out << m;
                }
            }
        }
        return out;
    }

    static QString cacheFile()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/seven-wmp/library.json");
    }
    void load()
    {
        QFile f(cacheFile());
        if (!f.open(QIODevice::ReadOnly)) {
            return;
        }
        const auto arr = QJsonDocument::fromJson(f.readAll()).array();
        for (const auto &v : arr) {
            const auto o = v.toObject();
            MediaItem m;
            m.path = o[u"path"].toString();
            m.kind = MediaItem::Kind(o[u"kind"].toInt());
            m.title = o[u"title"].toString();
            m.artist = o[u"artist"].toString();
            m.albumArtist = o[u"albumArtist"].toString();
            m.album = o[u"album"].toString();
            m.genre = o[u"genre"].toString();
            m.composer = o[u"composer"].toString();
            m.track = o[u"track"].toInt();
            m.year = o[u"year"].toInt();
            m.duration = qint64(o[u"duration"].toDouble());
            m.size = qint64(o[u"size"].toDouble());
            m.mtime = qint64(o[u"mtime"].toDouble());
            m.rating = o[u"rating"].toInt();
            m.width = o[u"width"].toInt();
            m.height = o[u"height"].toInt();
            if (QFile::exists(m.path)) {
                m_items << m;
            }
        }
    }
    void save() const
    {
        QJsonArray arr;
        for (const auto &m : m_items) {
            QJsonObject o;
            o[u"path"] = m.path;
            o[u"kind"] = int(m.kind);
            o[u"title"] = m.title;
            o[u"artist"] = m.artist;
            o[u"albumArtist"] = m.albumArtist;
            o[u"album"] = m.album;
            o[u"genre"] = m.genre;
            o[u"composer"] = m.composer;
            o[u"track"] = m.track;
            o[u"year"] = m.year;
            o[u"duration"] = double(m.duration);
            o[u"size"] = double(m.size);
            o[u"mtime"] = double(m.mtime);
            o[u"rating"] = m.rating;
            o[u"width"] = m.width;
            o[u"height"] = m.height;
            arr << o;
        }
        QDir().mkpath(QFileInfo(cacheFile()).absolutePath());
        QFile f(cacheFile());
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        }
    }

    QList<MediaItem> m_items;
    bool m_busy = false;
};
