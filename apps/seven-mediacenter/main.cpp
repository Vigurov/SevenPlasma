#include "win7i18n.h"
/*
 * Windows Media Center (Windows 7, ehshell.exe) on Qt Quick.
 *
 * The interface is QML (qml/), laid out after the program's own markup (Microsoft.MediaCenter.Shell.dll MCML:
 * a 1366x768 "ten foot" canvas, Segoe Media Center fonts, the colours of Colors.mcml) with its pictures
 * (ehres.dll, extracted to <data>/seven-mediacenter/res). This file has what QML cannot do itself: the media
 * libraries of the user's Music, Pictures and Videos folders (tags and cover art read with ffprobe / ffmpeg,
 * cached in ~/.cache/seven-mediacenter), the settings and the games of the extras library.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent>

namespace
{
const char *const kApp = "seven-mediacenter";

QString cacheDir()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QLatin1Char('/') + QLatin1String(kApp);
    QDir().mkpath(d + QStringLiteral("/covers"));
    QDir().mkpath(d + QStringLiteral("/thumbs"));
    return d;
}

QString hashName(const QString &s)
{
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Md5).toHex());
}

const QStringList kAudio{QStringLiteral("*.mp3"), QStringLiteral("*.flac"), QStringLiteral("*.ogg"), QStringLiteral("*.oga"), QStringLiteral("*.opus"),
                         QStringLiteral("*.m4a"), QStringLiteral("*.aac"), QStringLiteral("*.wma"), QStringLiteral("*.wav")};
const QStringList kPictures{QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png"), QStringLiteral("*.gif"), QStringLiteral("*.bmp"),
                            QStringLiteral("*.webp"), QStringLiteral("*.tif"), QStringLiteral("*.tiff")};
const QStringList kVideos{QStringLiteral("*.mp4"), QStringLiteral("*.mkv"), QStringLiteral("*.avi"), QStringLiteral("*.wmv"), QStringLiteral("*.mov"),
                          QStringLiteral("*.webm"), QStringLiteral("*.m4v"), QStringLiteral("*.mpg"), QStringLiteral("*.mpeg"), QStringLiteral("*.ts"),
                          QStringLiteral("*.dvr-ms"), QStringLiteral("*.wtv")};

QStringList filesUnder(const QString &dir, const QStringList &patterns)
{
    QStringList out;
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        return out;
    }
    QDirIterator it(dir, patterns, QDir::Files | QDir::Readable, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    while (it.hasNext()) {
        out << it.next();
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

/** a song's tags and the length (ffprobe), the cover art (a picture in the folder, else the embedded one) */
QJsonObject probeSong(const QString &path)
{
    QJsonObject song{{QStringLiteral("file"), path}};
    QProcess p;
    p.start(QStringLiteral("ffprobe"), {QStringLiteral("-v"), QStringLiteral("quiet"), QStringLiteral("-print_format"), QStringLiteral("json"),
                                        QStringLiteral("-show_format"), QStringLiteral("-show_streams"), path});
    p.waitForFinished(15000);
    const QJsonObject j = QJsonDocument::fromJson(p.readAllStandardOutput()).object();
    const QJsonObject fmt = j.value(QStringLiteral("format")).toObject();
    QJsonObject tags;
    const QJsonObject raw = fmt.value(QStringLiteral("tags")).toObject();
    for (auto it = raw.begin(); it != raw.end(); ++it) {
        tags.insert(it.key().toLower(), it.value());
    }
    const QFileInfo fi(path);
    song.insert(QStringLiteral("title"), tags.value(QStringLiteral("title")).toString(fi.completeBaseName()));
    song.insert(QStringLiteral("artist"), tags.value(QStringLiteral("artist")).toString());
    song.insert(QStringLiteral("albumArtist"), tags.value(QStringLiteral("album_artist")).toString(tags.value(QStringLiteral("artist")).toString()));
    song.insert(QStringLiteral("album"), tags.value(QStringLiteral("album")).toString());
    song.insert(QStringLiteral("genre"), tags.value(QStringLiteral("genre")).toString());
    song.insert(QStringLiteral("composer"), tags.value(QStringLiteral("composer")).toString());
    song.insert(QStringLiteral("year"), tags.value(QStringLiteral("date")).toString().left(4));
    song.insert(QStringLiteral("track"), tags.value(QStringLiteral("track")).toString().section(QLatin1Char('/'), 0, 0).toInt());
    song.insert(QStringLiteral("duration"), fmt.value(QStringLiteral("duration")).toString().toDouble());
    song.insert(QStringLiteral("mtime"), double(fi.lastModified().toSecsSinceEpoch()));

    // cover: Folder.jpg / cover.jpg / AlbumArt_{..}_Large.jpg (Windows Media Player's), else the picture in the file
    QString cover;
    const QDir dir = fi.dir();
    for (const QString &n : dir.entryList({QStringLiteral("folder.jpg"), QStringLiteral("cover.jpg"), QStringLiteral("cover.png"), QStringLiteral("front.jpg"),
                                           QStringLiteral("AlbumArt*_Large.jpg"), QStringLiteral("AlbumArtSmall.jpg")},
                                          QDir::Files | QDir::Hidden)) {
        cover = dir.filePath(n);
        break;
    }
    bool hasPic = false;
    for (const QJsonValue &s : j.value(QStringLiteral("streams")).toArray()) {
        if (s.toObject().value(QStringLiteral("disposition")).toObject().value(QStringLiteral("attached_pic")).toInt() == 1) {
            hasPic = true;
        }
    }
    if (cover.isEmpty() && hasPic) {
        const QString key = song.value(QStringLiteral("album")).toString().isEmpty() ? path : song.value(QStringLiteral("albumArtist")).toString() + QLatin1Char('|') + song.value(QStringLiteral("album")).toString();
        const QString out = cacheDir() + QStringLiteral("/covers/") + hashName(key) + QStringLiteral(".jpg");
        if (!QFileInfo::exists(out)) {
            QProcess f;
            f.start(QStringLiteral("ffmpeg"), {QStringLiteral("-v"), QStringLiteral("quiet"), QStringLiteral("-y"), QStringLiteral("-i"), path, QStringLiteral("-an"),
                                               QStringLiteral("-vf"), QStringLiteral("scale=300:-2"), QStringLiteral("-frames:v"), QStringLiteral("1"), out});
            f.waitForFinished(15000);
        }
        if (QFileInfo::exists(out)) {
            cover = out;
        }
    }
    song.insert(QStringLiteral("cover"), cover.isEmpty() ? QString() : QUrl::fromLocalFile(cover).toString());
    return song;
}

/** a frame of a video for its tile */
double videoDuration(const QString &path);
// the frame Windows 7 shows for a video: 10 s in (a third of a shorter one)
QString videoThumb(const QString &path)
{
    const QString out = cacheDir() + QStringLiteral("/thumbs/") + hashName(path + QString::number(QFileInfo(path).lastModified().toSecsSinceEpoch()) + QStringLiteral("t10")) + QStringLiteral(".jpg");
    if (!QFileInfo::exists(out)) {
        const double len = videoDuration(path);
        const QString at = QString::number(len > 0 && len < 30 ? len / 3 : 10.0, 'f', 2);
        QProcess f;
        f.start(QStringLiteral("ffmpeg"), {QStringLiteral("-v"), QStringLiteral("quiet"), QStringLiteral("-y"), QStringLiteral("-ss"), at,
                                           QStringLiteral("-i"), path, QStringLiteral("-vf"), QStringLiteral("scale=320:-2"), QStringLiteral("-frames:v"),
                                           QStringLiteral("1"), out});
        f.waitForFinished(20000);
        if (!QFileInfo::exists(out)) { // too short
            f.start(QStringLiteral("ffmpeg"), {QStringLiteral("-v"), QStringLiteral("quiet"), QStringLiteral("-y"), QStringLiteral("-i"), path,
                                               QStringLiteral("-vf"), QStringLiteral("scale=320:-2"), QStringLiteral("-frames:v"), QStringLiteral("1"), out});
            f.waitForFinished(20000);
        }
    }
    return QFileInfo::exists(out) ? QUrl::fromLocalFile(out).toString() : QString();
}

// a video's length in seconds (ffprobe)
double videoDuration(const QString &path)
{
    QProcess f;
    f.start(QStringLiteral("ffprobe"), {QStringLiteral("-v"), QStringLiteral("quiet"), QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
                                        QStringLiteral("-of"), QStringLiteral("csv=p=0"), path});
    f.waitForFinished(10000);
    return QString::fromLatin1(f.readAllStandardOutput()).trimmed().toDouble();
}
}

// a JPEG's EXIF: the date it was taken (DateTimeOriginal) and the Windows rating (0x4746, 0-5 stars)
struct PhotoInfo {
    QDateTime taken;
    int rating = 0;
};
static PhotoInfo readExif(const QString &path)
{
    PhotoInfo info;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return info;
    }
    const QByteArray head = f.read(256 * 1024);
    if (!head.startsWith("\xFF\xD8")) {
        return info;
    }
    int pos = 2;
    while (pos + 4 < head.size() && uchar(head[pos]) == 0xFF) {
        const int marker = uchar(head[pos + 1]);
        const int len = (uchar(head[pos + 2]) << 8) | uchar(head[pos + 3]);
        if (marker == 0xE1 && head.mid(pos + 4, 6) == QByteArray("Exif\0\0", 6)) {
            const QByteArray t = head.mid(pos + 10, len - 8);
            const bool le = t.startsWith("II");
            auto u16 = [&](int o) -> int { return o + 2 > t.size() ? 0 : le ? (uchar(t[o]) | uchar(t[o + 1]) << 8) : (uchar(t[o]) << 8 | uchar(t[o + 1])); };
            auto u32 = [&](int o) -> quint32 { return quint32(u16(le ? o : o + 2)) | quint32(u16(le ? o + 2 : o)) << 16; };
            auto walk = [&](int ifd, auto &&onTag) {
                const int n = u16(ifd);
                for (int i = 0; i < n && ifd + 2 + i * 12 + 12 <= t.size(); ++i) {
                    const int e = ifd + 2 + i * 12;
                    onTag(u16(e), u16(e + 2), u32(e + 4), e + 8);
                }
            };
            int exifIfd = 0;
            walk(int(u32(4)), [&](int tag, int, quint32, int val) {
                if (tag == 0x4746) {
                    info.rating = u16(val);
                }
                if (tag == 0x8769) {
                    exifIfd = int(u32(val));
                }
            });
            if (exifIfd > 0) {
                walk(exifIfd, [&](int tag, int, quint32 count, int val) {
                    if (tag == 0x9003 && count >= 19) {
                        info.taken = QDateTime::fromString(QString::fromLatin1(t.mid(int(u32(val)), 19)), QStringLiteral("yyyy:MM:dd HH:mm:ss"));
                    }
                });
            }
            break;
        }
        pos += 2 + len;
    }
    return info;
}

/** The media libraries and the settings, for QML ("library") */
class Library : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList albums READ albums NOTIFY musicChanged)
    Q_PROPERTY(QVariantList songs READ songs NOTIFY musicChanged)
    Q_PROPERTY(QVariantList artists READ artists NOTIFY musicChanged)
    Q_PROPERTY(QVariantList genres READ genres NOTIFY musicChanged)
    Q_PROPERTY(QVariantList pictureFolders READ pictureFolders NOTIFY picturesChanged)
    Q_PROPERTY(QVariantList videos READ videos NOTIFY videosChanged)
    Q_PROPERTY(QVariantList games READ games CONSTANT)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QString res READ res CONSTANT)
    Q_PROPERTY(bool firstRun READ firstRun WRITE setFirstRun NOTIFY firstRunChanged)
    Q_PROPERTY(QString musicFolder READ musicFolder CONSTANT)
    Q_PROPERTY(QString picturesFolder READ picturesFolder CONSTANT)
    Q_PROPERTY(QString videosFolder READ videosFolder CONSTANT)

public:
    Library()
    {
        m_res = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QLatin1String(kApp) + QStringLiteral("/res"), QStandardPaths::LocateDirectory);
        loadCache();
    }

    QVariantList albums() const
    {
        return m_albums;
    }
    QVariantList songs() const
    {
        return m_songList;
    }
    QVariantList artists() const
    {
        return m_artists;
    }
    QVariantList genres() const
    {
        return m_genres;
    }
    QVariantList pictureFolders() const
    {
        return m_pictureFolders;
    }
    QVariantList videos() const
    {
        return m_videos;
    }
    bool scanning() const
    {
        return m_scanning;
    }
    QString res() const
    {
        return m_res.isEmpty() ? QString() : QUrl::fromLocalFile(m_res).toString() + QLatin1Char('/');
    }
    QString musicFolder() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    }
    QString picturesFolder() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    }
    QString videosFolder() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    }

    bool firstRun() const
    {
        return !QSettings(QStringLiteral("win7"), QLatin1String(kApp)).value(QStringLiteral("SetupDone"), false).toBool();
    }
    void setFirstRun(bool v)
    {
        QSettings(QStringLiteral("win7"), QLatin1String(kApp)).setValue(QStringLiteral("SetupDone"), !v);
        Q_EMIT firstRunChanged();
    }

    /** the games of Windows 7 (installed by SevenPlasma from the image, run through Wine) for the extras library */
    QVariantList games() const
    {
        QVariantList out;
        const QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
        QStringList seen;
        for (const QString &d : dirs) {
            for (const QString &f : QDir(d).entryList({QStringLiteral("seven_*.desktop"), QStringLiteral("seven_*.desktop")}, QDir::Files, QDir::Name)) {
                if (seen.contains(f)) {
                    continue;
                }
                seen << f;
                QSettings s(QDir(d).filePath(f), QSettings::IniFormat);
                s.beginGroup(QStringLiteral("Desktop Entry"));
                QString name = s.value(QStringLiteral("Name")).toString();
                for (const QString &x : Win7I18n::keySuffixes()) {
                    const QString v = s.value(QStringLiteral("Name[") + x + QLatin1Char(']')).toString();
                    if (!v.isEmpty()) {
                        name = v;
                        break;
                    }
                }
                out.append(QVariantMap{{QStringLiteral("name"), name},
                                       {QStringLiteral("icon"), s.value(QStringLiteral("Icon")).toString()},
                                       {QStringLiteral("desktop"), QDir(d).filePath(f)}});
            }
        }
        return out;
    }

    Q_INVOKABLE void launchDesktop(const QString &desktopFile)
    {
        // kioclient (kde-cli-tools) is there on every Plasma system
        QProcess::startDetached(QStringLiteral("kioclient"), {QStringLiteral("exec"), desktopFile});
    }
    Q_INVOKABLE void run(const QString &program, const QStringList &args = {})
    {
        QProcess::startDetached(program, args);
    }
    Q_INVOKABLE QString iconUrl(const QString &iconName, int size)
    {
        // themed icons for QML Image: the file of the hicolor theme or the current one
        const QIcon icon = QIcon::fromTheme(iconName);
        if (icon.isNull()) {
            return {};
        }
        const QString out = cacheDir() + QStringLiteral("/thumbs/icon-") + hashName(iconName) + QLatin1Char('-') + QString::number(size) + QStringLiteral(".png");
        if (!QFileInfo::exists(out)) {
            icon.pixmap(size, size).save(out);
        }
        return QUrl::fromLocalFile(out).toString();
    }

    Q_INVOKABLE void scan()
    {
        if (m_scanning) {
            return;
        }
        m_scanning = true;
        Q_EMIT scanningChanged();
        // pictures and videos are quick to list; songs are read in parallel, unchanged ones come from the cache
        scanPictures();
        auto *watcher = new QFutureWatcher<QVariantList>(this);
        connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher]() {
            m_videos = watcher->result();
            watcher->deleteLater();
            Q_EMIT videosChanged();
            saveCache();
        });
        const QString videosDir = videosFolder();
        watcher->setFuture(QtConcurrent::run([videosDir]() {
            QVariantList out;
            for (const QString &f : filesUnder(videosDir, kVideos)) {
                const QFileInfo fi(f);
                out.append(QVariantMap{{QStringLiteral("name"), fi.completeBaseName()},
                                       {QStringLiteral("file"), QUrl::fromLocalFile(f).toString()},
                                       {QStringLiteral("folder"), QDir(videosDir).relativeFilePath(fi.absolutePath())},
                                       {QStringLiteral("date"), fi.lastModified().toString(QStringLiteral("dd.MM.yyyy"))},
                                       {QStringLiteral("thumb"), videoThumb(f)},
                                       {QStringLiteral("duration"), videoDuration(f)}});
            }
            return out;
        }));

        const QStringList files = filesUnder(musicFolder(), kAudio);
        QHash<QString, QJsonObject> cached = m_songCache;
        auto *mw = new QFutureWatcher<QJsonObject>(this);
        connect(mw, &QFutureWatcher<QJsonObject>::finished, this, [this, mw]() {
            QHash<QString, QJsonObject> songs;
            for (const QJsonObject &s : mw->future().results()) {
                songs.insert(s.value(QStringLiteral("file")).toString(), s);
            }
            mw->deleteLater();
            m_songCache = songs;
            buildMusic();
            saveCache();
            m_scanning = false;
            Q_EMIT scanningChanged();
        });
        mw->setFuture(QtConcurrent::mapped(files, [cached](const QString &f) {
            const auto it = cached.constFind(f);
            if (it != cached.constEnd() && it->value(QStringLiteral("mtime")).toDouble() == double(QFileInfo(f).lastModified().toSecsSinceEpoch())) {
                return *it;
            }
            return probeSong(f);
        }));
    }

Q_SIGNALS:
    void musicChanged();
    void picturesChanged();
    void videosChanged();
    void scanningChanged();
    void firstRunChanged();

private:
    void scanPictures()
    {
        // the folders of the Pictures folder (and its own pictures as one more folder), as the picture library shows them
        QVariantList folders;
        const QString root = picturesFolder();
        auto addFolder = [&folders](const QString &dir, const QString &name, bool recursive) {
            QStringList pics;
            if (recursive) {
                pics = filesUnder(dir, kPictures);
            } else {
                for (const QString &n : QDir(dir).entryList(kPictures, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
                    pics << QDir(dir).filePath(n);
                }
            }
            if (pics.isEmpty()) {
                return;
            }
            QVariantList items;
            for (const QString &p : std::as_const(pics)) {
                const QFileInfo fi(p);
                // Windows 7 shows the date the picture was taken and its rating, from its EXIF
                const PhotoInfo exif = readExif(p);
                const QDateTime when = exif.taken.isValid() ? exif.taken : fi.lastModified();
                items.append(QVariantMap{{QStringLiteral("name"), fi.completeBaseName()},
                                         {QStringLiteral("file"), QUrl::fromLocalFile(p).toString()},
                                         {QStringLiteral("date"), when.toString(QStringLiteral("dd.MM.yyyy"))},
                                         {QStringLiteral("time"), double(when.toSecsSinceEpoch())},
                                         {QStringLiteral("rating"), exif.rating}});
            }
            folders.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("items"), items}});
        };
        for (const QString &d : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
            addFolder(QDir(root).filePath(d), d, true);
        }
        addFolder(root, QFileInfo(root).fileName(), false);
        m_pictureFolders = folders;
        Q_EMIT picturesChanged();
    }

    void buildMusic()
    {
        // albums by (album artist, album); songs without an album become albums of their own title
        QMap<QString, QVariantMap> albums;
        QMap<QString, QVariantList> albumSongs;
        QVariantList songs;
        QMap<QString, int> artists, genres;
        for (const QJsonObject &s : std::as_const(m_songCache)) {
            QVariantMap song = s.toVariantMap();
            song.insert(QStringLiteral("url"), QUrl::fromLocalFile(s.value(QStringLiteral("file")).toString()).toString());
            const QString album = s.value(QStringLiteral("album")).toString();
            const QString artist = s.value(QStringLiteral("albumArtist")).toString();
            const QString key = artist.toLower() + QLatin1Char('|') + (album.isEmpty() ? s.value(QStringLiteral("file")).toString() : album.toLower());
            if (!albums.contains(key)) {
                albums.insert(key, QVariantMap{{QStringLiteral("album"), album.isEmpty() ? s.value(QStringLiteral("title")).toString() : album},
                                               {QStringLiteral("artist"), artist},
                                               {QStringLiteral("year"), s.value(QStringLiteral("year")).toString()},
                                               {QStringLiteral("cover"), s.value(QStringLiteral("cover")).toString()}});
            } else if (albums[key].value(QStringLiteral("cover")).toString().isEmpty()) {
                albums[key].insert(QStringLiteral("cover"), s.value(QStringLiteral("cover")).toString());
            }
            albumSongs[key].append(song);
            songs.append(song);
            if (!s.value(QStringLiteral("artist")).toString().isEmpty()) {
                artists[s.value(QStringLiteral("artist")).toString()]++;
            }
            if (!s.value(QStringLiteral("genre")).toString().isEmpty()) {
                genres[s.value(QStringLiteral("genre")).toString()]++;
            }
        }
        QVariantList albumList;
        for (auto it = albums.begin(); it != albums.end(); ++it) {
            QVariantList tracks = albumSongs.value(it.key());
            std::sort(tracks.begin(), tracks.end(), [](const QVariant &a, const QVariant &b) {
                return a.toMap().value(QStringLiteral("track")).toInt() < b.toMap().value(QStringLiteral("track")).toInt();
            });
            double total = 0;
            for (const QVariant &t : std::as_const(tracks)) {
                total += t.toMap().value(QStringLiteral("duration")).toDouble();
            }
            QVariantMap a = it.value();
            a.insert(QStringLiteral("tracks"), tracks);
            a.insert(QStringLiteral("duration"), total);
            albumList.append(a);
        }
        auto byKey = [](const char *k) {
            return [k](const QVariant &a, const QVariant &b) {
                return QString::localeAwareCompare(a.toMap().value(QLatin1String(k)).toString(), b.toMap().value(QLatin1String(k)).toString()) < 0;
            };
        };
        std::sort(albumList.begin(), albumList.end(), byKey("album"));
        std::sort(songs.begin(), songs.end(), byKey("title"));
        m_albums = albumList;
        m_songList = songs;
        m_artists.clear();
        for (auto it = artists.begin(); it != artists.end(); ++it) {
            m_artists.append(QVariantMap{{QStringLiteral("name"), it.key()}, {QStringLiteral("count"), it.value()}});
        }
        m_genres.clear();
        for (auto it = genres.begin(); it != genres.end(); ++it) {
            m_genres.append(QVariantMap{{QStringLiteral("name"), it.key()}, {QStringLiteral("count"), it.value()}});
        }
        Q_EMIT musicChanged();
    }

    void loadCache()
    {
        QFile f(cacheDir() + QStringLiteral("/library.json"));
        if (!f.open(QIODevice::ReadOnly)) {
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        for (const QJsonValue &v : o.value(QStringLiteral("songs")).toArray()) {
            m_songCache.insert(v.toObject().value(QStringLiteral("file")).toString(), v.toObject());
        }
        m_videos = o.value(QStringLiteral("videos")).toArray().toVariantList();
        buildMusic();
    }

    void saveCache()
    {
        QJsonArray songs;
        for (const QJsonObject &s : std::as_const(m_songCache)) {
            songs.append(s);
        }
        QSaveFile f(cacheDir() + QStringLiteral("/library.json"));
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(QJsonObject{{QStringLiteral("songs"), songs}, {QStringLiteral("videos"), QJsonArray::fromVariantList(m_videos)}}).toJson(QJsonDocument::Compact));
            f.commit();
        }
    }

    QString m_res;
    QHash<QString, QJsonObject> m_songCache;
    QVariantList m_albums, m_songList, m_artists, m_genres, m_pictureFolders, m_videos;
    bool m_scanning = false;
};

// the mouse moving anywhere over the window (for the mouse controls), without taking the hover from the items
// under the pointer, which a HoverHandler over the whole interface does
class PointerWatch : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
Q_SIGNALS:
    void moved(QPointF pos);

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::MouseMove || e->type() == QEvent::HoverMove) {
            Q_EMIT moved(static_cast<QSinglePointEvent *>(e)->scenePosition());
        }
        return QObject::eventFilter(o, e);
    }
};

int main(int argc, char **argv)
{
    // the whole interface is a 1366x768 canvas scaled to the window: distance field text gets pointed corners
    // when scaled, curve rendering draws the glyph outlines smooth at any size
    QQuickWindow::setTextRenderType(qEnvironmentVariableIsSet("SEVEN_MC_DF_TEXT") ? QQuickWindow::QtTextRendering
                                                                                  : QQuickWindow::CurveTextRendering);
    // Qt's PipeWire audio looks the default output up by name and misses Bluetooth ones (PipeWire names their node
    // differently), so the sound went to the built-in speakers; the PulseAudio backend follows the default output
    if (!qEnvironmentVariableIsSet("QT_AUDIO_BACKEND"))
        qputenv("QT_AUDIO_BACKEND", "pulseaudio");
    QGuiApplication app(argc, argv);
    Win7I18n::install(&app);
    app.setApplicationName(QLatin1String(kApp));
    app.setApplicationDisplayName(tr7("Windows Media Center"));
    app.setDesktopFileName(QLatin1String(kApp));
    app.setWindowIcon(QIcon::fromTheme(QLatin1String(kApp)));

    Library library;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("library"), &library);
    // test hook: --page KIND opens that page of the menus (music, pictures, videos, extras...) over the start menu
    const int pageArg = app.arguments().indexOf(QStringLiteral("--page"));
    engine.rootContext()->setContextProperty(QStringLiteral("startPage"), pageArg > 0 ? app.arguments().value(pageArg + 1) : QString());
    engine.rootContext()->setContextProperty(QStringLiteral("startMaximized"), app.arguments().contains(QStringLiteral("--maximized")));
    engine.rootContext()->setContextProperty(QStringLiteral("startFullScreen"), app.arguments().contains(QStringLiteral("/directmedia")) || app.arguments().contains(QStringLiteral("--fullscreen")));
    PointerWatch pointer;
    engine.rootContext()->setContextProperty(QStringLiteral("pointer"), &pointer);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    engine.rootObjects().constFirst()->installEventFilter(&pointer);
    library.scan();

    // test hook: SEVEN_MC_TEST_INPUT="2000;move 370,115;500;key Down;click 860,224" plays mouse moves, clicks and keys
    // (window pixels) after the given pauses, to compare the states with Windows 7 screenshots
    const QString script = qEnvironmentVariable("SEVEN_MC_TEST_INPUT");
    if (!script.isEmpty()) {
        auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        int at = 0;
        for (const QString &step : script.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            bool num = false;
            const int ms = step.trimmed().toInt(&num);
            if (num) {
                at += ms;
                continue;
            }
            QTimer::singleShot(at, win, [win, step] {
                const QStringList w = step.trimmed().split(QLatin1Char(' '));
                const QStringList xy = w.value(1).split(QLatin1Char(','));
                const QPointF p(xy.value(0).toDouble(), xy.value(1).toDouble());
                if (w[0] == QLatin1String("move") || w[0] == QLatin1String("click")) {
                    QMouseEvent move(QEvent::MouseMove, p, win->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(win, &move);
                }
                if (w[0] == QLatin1String("click")) {
                    QMouseEvent press(QEvent::MouseButtonPress, p, win->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(win, &press);
                    QMouseEvent release(QEvent::MouseButtonRelease, p, win->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(win, &release);
                }
                if (w[0] == QLatin1String("key")) {
                    static const QHash<QString, int> keys{{QStringLiteral("Up"), Qt::Key_Up}, {QStringLiteral("Down"), Qt::Key_Down},
                                                          {QStringLiteral("Left"), Qt::Key_Left}, {QStringLiteral("Right"), Qt::Key_Right},
                                                          {QStringLiteral("Return"), Qt::Key_Return}, {QStringLiteral("Back"), Qt::Key_Backspace}};
                    const int k = keys.value(w.value(1));
                    QKeyEvent press(QEvent::KeyPress, k, Qt::NoModifier);
                    QCoreApplication::sendEvent(win, &press);
                    QKeyEvent release(QEvent::KeyRelease, k, Qt::NoModifier);
                    QCoreApplication::sendEvent(win, &release);
                }
            });
        }
    }
    return app.exec();
}

#include "main.moc"
