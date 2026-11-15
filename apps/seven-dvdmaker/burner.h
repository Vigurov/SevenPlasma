#pragma once
/*
 * Making the DVD-Video disc: the videos become MPEG-2 titles (ffmpeg), the pictures one slide show title (with
 * its music and cross fades), the menus are still pictures of the chosen style with their button highlights
 * (spumux), dvdauthor builds VIDEO_TS with the menu commands of Windows DVD Maker's playback settings, and
 * growisofs burns it.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "menurender.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

namespace Dvd
{
struct Item {
    QString path;
    bool picture = false;
    double duration = 0; // seconds (videos)
    QImage thumb;
};

struct Settings {
    enum Playback { StartWithMenu, PlayThenMenu, Loop };
    Playback playback = StartWithMenu;
    bool wide = false;   // 16:9
    bool pal = true;
    int speed = 0;       // 0 fastest, 1 medium, 2 slow
    QString tempDir;
    QString styleGuid = QStringLiteral("DVDMenu-Full Screen");
    Texts texts;
    QStringList music;   // slide show music
    int pictureSeconds = 7;
    QString transition = QStringLiteral("Cross fade");
    bool panZoom = false;
    QString foreground, background, menuAudio; // custom style files ("" = from the content)
};

inline double probeDuration(const QString &file)
{
    QProcess p;
    p.start(QStringLiteral("ffprobe"), {QStringLiteral("-v"), QStringLiteral("quiet"), QStringLiteral("-print_format"), QStringLiteral("json"),
                                        QStringLiteral("-show_format"), file});
    p.waitForFinished(20000);
    return QJsonDocument::fromJson(p.readAllStandardOutput()).object().value(QStringLiteral("format")).toObject().value(QStringLiteral("duration")).toString().toDouble();
}

inline QImage videoFrame(const QString &file, double at)
{
    QTemporaryDir tmp;
    const QString out = tmp.filePath(QStringLiteral("f.png"));
    QProcess p;
    p.start(QStringLiteral("ffmpeg"), {QStringLiteral("-v"), QStringLiteral("quiet"), QStringLiteral("-y"), QStringLiteral("-ss"), QString::number(at, 'f', 2),
                                       QStringLiteral("-i"), file, QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-vf"), QStringLiteral("scale=480:-2"), out});
    p.waitForFinished(30000);
    QImage im(out);
    if (im.isNull() && at > 0) {
        return videoFrame(file, 0);
    }
    return im;
}

class Burner : public QObject
{
    Q_OBJECT
public:
    Burner(const QList<Item> &items, const Settings &s, const QString &device, QObject *parent = nullptr)
        : QObject(parent)
        , m_items(items)
        , m_s(s)
        , m_device(device)
    {
    }

    /** the image of the content shown in the menus: the first video's frame, else the first picture */
    static QImage contentPicture(const QList<Item> &items)
    {
        for (const Item &i : items) {
            if (!i.picture) {
                return videoFrame(i.path, qMin(10.0, i.duration / 3));
            }
        }
        for (const Item &i : items) {
            if (i.picture) {
                return QImage(i.path);
            }
        }
        return {};
    }

    /** the titles of the disc: each video, the pictures together as one slide show (where the first picture is) */
    static QList<QList<Item>> titles(const QList<Item> &items)
    {
        QList<QList<Item>> out;
        int slides = -1;
        for (const Item &i : items) {
            if (i.picture) {
                if (slides < 0) {
                    slides = out.size();
                    out.append(QList<Item>());
                }
                out[slides].append(i);
            } else {
                out.append({i});
            }
        }
        return out;
    }

    static QImage titleThumb(const QList<Item> &t)
    {
        if (t.isEmpty()) {
            return {};
        }
        return t.first().picture ? QImage(t.first().path) : (t.first().thumb.isNull() ? videoFrame(t.first().path, qMin(10.0, t.first().duration / 3)) : t.first().thumb);
    }

    QSize frameSize() const
    {
        return QSize(720, m_s.pal ? 576 : 480);
    }

    void start()
    {
        m_dir = m_s.tempDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/seven-dvdmaker-") + QString::number(QCoreApplication::applicationPid())
                                      : m_s.tempDir + QStringLiteral("/seven-dvdmaker");
        QDir(m_dir).removeRecursively();
        QDir().mkpath(m_dir);
        m_titles = titles(m_items);
        m_total = 0;
        for (const auto &t : std::as_const(m_titles)) {
            m_total += titleSeconds(t);
        }
        m_steps.clear();
        // 1. the titles
        for (int i = 0; i < m_titles.size(); ++i) {
            m_steps.append([this, i]() { encodeTitle(i); });
        }
        // 2. the menus, 3. VIDEO_TS, 4. burning
        m_steps.append([this]() { makeMenus(); });
        m_steps.append([this]() { author(); });
        m_steps.append([this]() { burn(); });
        m_step = -1;
        m_clock.start();
        next();
    }

    void cancel()
    {
        m_cancelled = true;
        if (m_proc) {
            m_proc->kill();
        }
    }

    QString workDir() const
    {
        return m_dir;
    }
    /** the temporary files go when the disc could not be made */
    void cleanup()
    {
        if (!m_dir.isEmpty()) {
            QDir(m_dir).removeRecursively();
        }
    }

Q_SIGNALS:
    void progress(int percent, const QString &status);
    void finished(bool ok, const QString &error);

private:
    double titleSeconds(const QList<Item> &t) const
    {
        if (t.isEmpty()) {
            return 0;
        }
        if (t.first().picture) {
            return t.size() * m_s.pictureSeconds;
        }
        return t.first().duration;
    }

    // the video bitrate that fills a single-layer DVD (4.38 GB) with all titles, at most 8 Mbit/s
    int videoKbps() const
    {
        const double bits = 4.2e9 * 8;
        const double secs = qMax(60.0, m_total + 60 * m_titles.size());
        return int(qBound(1500.0, bits / secs / 1000 - 256, 8000.0));
    }

    QStringList targetArgs() const
    {
        return {QStringLiteral("-target"), m_s.pal ? QStringLiteral("pal-dvd") : QStringLiteral("ntsc-dvd"), QStringLiteral("-aspect"),
                m_s.wide ? QStringLiteral("16:9") : QStringLiteral("4:3"), QStringLiteral("-b:v"), QString::number(videoKbps()) + QLatin1Char('k'),
                QStringLiteral("-maxrate"), QStringLiteral("9000k"), QStringLiteral("-bufsize"), QStringLiteral("1835k"), QStringLiteral("-ar"),
                QStringLiteral("48000"), QStringLiteral("-ac"), QStringLiteral("2"), QStringLiteral("-b:a"), QStringLiteral("192k")};
    }

    /** fit the picture into the DVD's display shape (4:3 768x576 / 16:9 1024x576 at PAL), black bars, stored 720 wide */
    QString fitFilter() const
    {
        const int h = frameSize().height();
        const int dw = m_s.wide ? h * 16 / 9 : h * 4 / 3;
        return QStringLiteral("scale=%1:%2:force_original_aspect_ratio=decrease,pad=%1:%2:(ow-iw)/2:(oh-ih)/2:black,scale=720:%2,setdar=%3,fps=%4,format=yuv420p")
            .arg((dw + 1) & ~1)
            .arg(h)
            .arg(m_s.wide ? QStringLiteral("16/9") : QStringLiteral("4/3"))
            .arg(m_s.pal ? QStringLiteral("25") : QStringLiteral("30000/1001"));
    }

    void next()
    {
        if (m_cancelled) {
            Q_EMIT finished(false, QString());
            return;
        }
        ++m_step;
        if (m_step >= m_steps.size()) {
            QDir(m_dir).removeRecursively();
            Q_EMIT finished(true, QString());
            return;
        }
        m_steps[m_step]();
    }

    /** run a program; ffmpeg's "time=" (or growisofs' "%") moves the bar from `from` to `to` percent */
    void run(const QString &program, const QStringList &args, int from, int to, double seconds, const QString &status, const QString &stdinFile = {}, const QString &stdoutFile = {})
    {
        m_proc = new QProcess(this);
        m_proc->setWorkingDirectory(m_dir);
        if (!stdinFile.isEmpty()) {
            m_proc->setStandardInputFile(stdinFile);
        }
        if (!stdoutFile.isEmpty()) {
            m_proc->setStandardOutputFile(stdoutFile);
        }
        m_proc->setProcessChannelMode(stdoutFile.isEmpty() ? QProcess::MergedChannels : QProcess::SeparateChannels);
        auto *out = new QByteArray;
        auto parse = [this, out, from, to, seconds, status](const QByteArray &data) {
            out->append(data);
            if (out->size() > 20000) {
                out->remove(0, out->size() - 20000);
            }
            static const QRegularExpression timeRe(QStringLiteral("time=(\\d+):(\\d+):(\\d+\\.?\\d*)"));
            static const QRegularExpression pctRe(QStringLiteral("(\\d+\\.\\d+)% done"));
            double f = -1;
            auto m = timeRe.match(QString::fromLatin1(data));
            if (m.hasMatch() && seconds > 0) {
                f = (m.captured(1).toDouble() * 3600 + m.captured(2).toDouble() * 60 + m.captured(3).toDouble()) / seconds;
            }
            auto pm = pctRe.match(QString::fromLatin1(data));
            if (pm.hasMatch()) {
                f = pm.captured(1).toDouble() / 100;
            }
            if (f >= 0) {
                Q_EMIT progress(from + int((to - from) * qBound(0.0, f, 1.0)), status);
            }
        };
        connect(m_proc, &QProcess::readyReadStandardOutput, this, [this, parse]() { parse(m_proc->readAllStandardOutput()); });
        connect(m_proc, &QProcess::readyReadStandardError, this, [this, parse]() { parse(m_proc->readAllStandardError()); });
        connect(m_proc, &QProcess::finished, this, [this, program, out, to, status](int code, QProcess::ExitStatus st) {
            const QString log = QString::fromLocal8Bit(*out);
            delete out;
            m_proc->deleteLater();
            m_proc = nullptr;
            if (m_cancelled) {
                Q_EMIT finished(false, QString());
                return;
            }
            if (st != QProcess::NormalExit || code != 0) {
                Q_EMIT finished(false, program + QStringLiteral(": ") + log.right(1500));
                return;
            }
            Q_EMIT progress(to, status);
            next();
        });
        connect(m_proc, &QProcess::errorOccurred, this, [this, program](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) {
                Q_EMIT finished(false, tr7("%1 is not installed.").arg(program));
            }
        });
        Q_EMIT progress(from, status);
        m_proc->start(program, args);
    }

    // the share of the bar: encoding 0-70, menus 70-74, authoring 74-80, burning 80-100
    int encodeFrom(int i) const
    {
        double done = 0;
        for (int k = 0; k < i; ++k) {
            done += titleSeconds(m_titles[k]);
        }
        return int(70 * done / qMax(1.0, m_total));
    }

    void encodeTitle(int i)
    {
        const QList<Item> &t = m_titles[i];
        const QString out = QStringLiteral("title%1.mpg").arg(i + 1);
        const int from = encodeFrom(i), to = encodeFrom(i + 1);
        const QString status = tr7("Creating DVD, please wait...");
        if (!t.first().picture) {
            QStringList args{QStringLiteral("-y"), QStringLiteral("-i"), t.first().path, QStringLiteral("-vf"), fitFilter()};
            args << targetArgs() << out;
            run(QStringLiteral("ffmpeg"), args, from, to, t.first().duration, status);
            return;
        }
        // the slide show: every picture for its seconds, cross-faded (or cut), with the music
        const int L = m_s.pictureSeconds;
        const double fade = m_s.transition == QLatin1String("Cut") ? 0.0 : qMin(1.0, L / 3.0);
        QStringList args{QStringLiteral("-y")};
        for (const Item &p : t) {
            args << QStringLiteral("-loop") << QStringLiteral("1") << QStringLiteral("-t") << QString::number(L + fade) << QStringLiteral("-i") << p.path;
        }
        for (const QString &m : m_s.music) {
            args << QStringLiteral("-i") << m;
        }
        QString fc;
        const int h = frameSize().height();
        for (int k = 0; k < t.size(); ++k) {
            QString zoom;
            if (m_s.panZoom) {
                zoom = QStringLiteral(",zoompan=z='min(zoom+0.0008,1.15)':d=%1:s=%2x%3:fps=%4").arg(int((L + fade) * 25)).arg(m_s.wide ? h * 16 / 9 : h * 4 / 3).arg(h).arg(25);
            }
            fc += QStringLiteral("[%1:v]%2%3,settb=AVTB[v%1];").arg(k).arg(fitFilter(), zoom);
        }
        QString last = QStringLiteral("v0");
        for (int k = 1; k < t.size(); ++k) {
            const QString o = QStringLiteral("x%1").arg(k);
            if (fade > 0) {
                const QString kind = m_s.transition == QLatin1String("Wipe") ? QStringLiteral("wipeleft")
                    : m_s.transition == QLatin1String("Dissolve")            ? QStringLiteral("dissolve")
                    : m_s.transition == QLatin1String("Pixelate")            ? QStringLiteral("pixelize")
                    : m_s.transition == QLatin1String("Page curl")           ? QStringLiteral("diagtl")
                    : m_s.transition == QLatin1String("Flip")                ? QStringLiteral("slideleft")
                    : m_s.transition == QLatin1String("Inset")               ? QStringLiteral("circleopen")
                                                                             : QStringLiteral("fade");
                fc += QStringLiteral("[%1][v%2]xfade=transition=%3:duration=%4:offset=%5[%6];").arg(last).arg(k).arg(kind).arg(fade).arg(k * L).arg(o);
            } else {
                fc += QStringLiteral("[%1][v%2]concat=n=2:v=1:a=0[%3];").arg(last).arg(k).arg(o);
            }
            last = o;
        }
        const double total = t.size() * L + (fade > 0 ? fade : 0);
        const int nPics = t.size();
        if (!m_s.music.isEmpty()) {
            QString a;
            for (int k = 0; k < m_s.music.size(); ++k) {
                a += QStringLiteral("[%1:a]").arg(nPics + k);
            }
            fc += a + QStringLiteral("concat=n=%1:v=0:a=1[a]").arg(m_s.music.size());
        } else {
            fc += QStringLiteral("anullsrc=r=48000:cl=stereo[a]");
        }
        args << QStringLiteral("-filter_complex") << fc << QStringLiteral("-map") << QLatin1Char('[') + last + QLatin1Char(']') << QStringLiteral("-map")
             << QStringLiteral("[a]") << QStringLiteral("-t") << QString::number(total);
        args << targetArgs() << out;
        run(QStringLiteral("ffmpeg"), args, from, to, total, status);
    }

    // ---------------------------------------------------------------- menus
    struct MenuFile {
        QString mpg;
        QList<MenuButton> buttons;
    };
    QList<MenuFile> m_menus; // 0 main, 1.. scenes pages, last notes
    int m_scenePages = 0;
    int m_scenesPerPage = 6;

    void makeMenus()
    {
        const Style *style = Styles::instance().find(m_s.styleGuid);
        if (!style) {
            Q_EMIT finished(false, tr7("The DVD menu styles are missing."));
            return;
        }
        MenuContent c;
        c.texts = m_s.texts;
        c.inset = m_s.foreground.isEmpty() ? contentPicture(m_items) : (QImage(m_s.foreground).isNull() ? videoFrame(m_s.foreground, 5) : QImage(m_s.foreground));
        c.background = m_s.background.isEmpty() ? c.inset : (QImage(m_s.background).isNull() ? videoFrame(m_s.background, 5) : QImage(m_s.background));
        c.hasNotes = !m_s.texts.notesText.isEmpty();
        QList<QImage> thumbs;
        for (const auto &t : std::as_const(m_titles)) {
            thumbs << titleThumb(t);
        }
        // scenes pages of 6 (fewer on the last page: the style's 2- or 4-button layout)
        m_scenesPerPage = 6;
        m_scenePages = qMax(1, int((thumbs.size() + 5) / 6));
        QList<QPair<QString, MenuContent>> pages;
        pages.append({QStringLiteral("MainMenu"), c});
        for (int pg = 0; pg < m_scenePages; ++pg) {
            MenuContent sc = c;
            sc.scenes = thumbs.mid(pg * 6, 6);
            sc.hasPrevious = pg > 0;
            sc.hasNext = pg < m_scenePages - 1;
            const int n = sc.scenes.size();
            pages.append({n <= 2 ? QStringLiteral("ScenesMenu2") : n <= 4 ? QStringLiteral("ScenesMenu4") : QStringLiteral("ScenesMenu6"), sc});
        }
        if (c.hasNotes) {
            pages.append({QStringLiteral("NotesMenu"), c});
        }
        m_menus.clear();
        m_menuJobs.clear();
        for (int i = 0; i < pages.size(); ++i) {
            QList<MenuButton> buttons;
            QImage hl;
            QImage img = Renderer::render(*style, pages[i].first, pages[i].second, frameSize(), &buttons, &hl);
            if (pages[i].first.startsWith(QLatin1String("Scenes")) && style->el.firstChildElement(QStringLiteral("Menus")).firstChildElement(pages[i].first).isNull()) {
                img = Renderer::render(*style, QStringLiteral("ScenesMenu6"), pages[i].second, frameSize(), &buttons, &hl);
            }
            const QString base = QStringLiteral("menu%1").arg(i);
            img.save(m_dir + QLatin1Char('/') + base + QStringLiteral(".png"));
            saveSubpicture(hl, m_dir + QLatin1Char('/') + base + QStringLiteral("-hl.png"), false);
            saveSubpicture(hl, m_dir + QLatin1Char('/') + base + QStringLiteral("-sel.png"), true);
            QFile x(m_dir + QLatin1Char('/') + base + QStringLiteral(".xml"));
            x.open(QIODevice::WriteOnly);
            QTextStream ts(&x);
            ts << "<subpictures><stream><spu force=\"yes\" start=\"00:00:00.00\" highlight=\"" << base << "-hl.png\" select=\"" << base << "-sel.png\">\n";
            for (const MenuButton &b : std::as_const(buttons)) {
                ts << "<button name=\"" << b.name << "\" x0=\"" << b.rect.left() << "\" y0=\"" << b.rect.top() << "\" x1=\"" << b.rect.right() - 1 << "\" y1=\""
                   << b.rect.bottom() - 1 << "\"/>\n";
            }
            ts << "</spu></stream></subpictures>\n";
            x.close();
            m_menus.append({base + QStringLiteral("-sp.mpg"), buttons});
            m_menuJobs.append(base);
        }
        m_menuJob = -1;
        nextMenuJob();
    }
    QStringList m_menuJobs;
    int m_menuJob = -1;
    bool m_menuSpu = false;

    // a DVD subpicture: transparent and one colour (selected: a lighter one)
    static void saveSubpicture(const QImage &hl, const QString &file, bool selected)
    {
        QImage out(hl.size(), QImage::Format_Indexed8);
        out.setColorTable({qRgba(0, 0, 0, 0), selected ? qRgba(255, 255, 255, 255) : qRgba(0, 96, 227, 255)});
        for (int y = 0; y < hl.height(); ++y) {
            const QRgb *src = reinterpret_cast<const QRgb *>(hl.constScanLine(y));
            uchar *dst = out.scanLine(y);
            for (int x = 0; x < hl.width(); ++x) {
                dst[x] = qAlpha(src[x]) > 128 ? 1 : 0;
            }
        }
        out.save(file);
    }

    // each menu: a 30 s still video with the menu's audio (or silence), then spumux adds its buttons
    void nextMenuJob()
    {
        if (!m_menuSpu) {
            ++m_menuJob;
        }
        if (m_menuJob >= m_menuJobs.size()) {
            Q_EMIT progress(74, tr7("Creating DVD, please wait..."));
            next();
            return;
        }
        const QString base = m_menuJobs[m_menuJob];
        const int pct = 70 + 4 * m_menuJob / qMax(1, int(m_menuJobs.size()));
        if (!m_menuSpu) {
            QStringList args{QStringLiteral("-y"), QStringLiteral("-loop"), QStringLiteral("1"), QStringLiteral("-i"), base + QStringLiteral(".png")};
            if (!m_s.menuAudio.isEmpty()) {
                args << QStringLiteral("-i") << m_s.menuAudio;
            } else {
                args << QStringLiteral("-f") << QStringLiteral("lavfi") << QStringLiteral("-i") << QStringLiteral("anullsrc=r=48000:cl=stereo");
            }
            args << QStringLiteral("-t") << QStringLiteral("30") << QStringLiteral("-map") << QStringLiteral("0:v") << QStringLiteral("-map") << QStringLiteral("1:a")
                 << QStringLiteral("-vf") << QStringLiteral("setdar=%1").arg(m_s.wide ? QStringLiteral("16/9") : QStringLiteral("4/3"));
            args << targetArgs() << base + QStringLiteral(".mpg");
            m_menuSpu = true;
            runChained(QStringLiteral("ffmpeg"), args, pct, {}, {});
        } else {
            m_menuSpu = false;
            runChained(QStringLiteral("spumux"), {base + QStringLiteral(".xml")}, pct, m_dir + QLatin1Char('/') + base + QStringLiteral(".mpg"),
                       m_dir + QLatin1Char('/') + base + QStringLiteral("-sp.mpg"));
        }
    }
    // like run(), but continues with the next menu job
    void runChained(const QString &program, const QStringList &args, int pct, const QString &in, const QString &out)
    {
        auto *proc = new QProcess(this);
        m_proc = proc;
        proc->setWorkingDirectory(m_dir);
        if (!in.isEmpty()) {
            proc->setStandardInputFile(in);
        }
        if (!out.isEmpty()) {
            proc->setStandardOutputFile(out);
        }
        connect(proc, &QProcess::finished, this, [this, proc, program](int code, QProcess::ExitStatus st) {
            const QString log = QString::fromLocal8Bit(proc->readAllStandardError() + proc->readAllStandardOutput());
            proc->deleteLater();
            m_proc = nullptr;
            if (m_cancelled) {
                Q_EMIT finished(false, QString());
                return;
            }
            if (st != QProcess::NormalExit || code != 0) {
                Q_EMIT finished(false, program + QStringLiteral(": ") + log.right(1500));
                return;
            }
            nextMenuJob();
        });
        connect(proc, &QProcess::errorOccurred, this, [this, program](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) {
                Q_EMIT finished(false, tr7("%1 is not installed.").arg(program));
            }
        });
        Q_EMIT progress(pct, tr7("Creating DVD, please wait..."));
        proc->start(program, args);
    }

    // ---------------------------------------------------------------- VIDEO_TS
    void author()
    {
        const int nTitles = m_titles.size();
        const int notesMenu = m_s.texts.notesText.isEmpty() ? 0 : 2 + m_scenePages; // menu numbers: 1 main, 2.. scenes, then notes
        QString x;
        QTextStream ts(&x);
        const QString video = QStringLiteral("<video format=\"%1\" aspect=\"%2\"%3/>")
                                  .arg(m_s.pal ? QStringLiteral("pal") : QStringLiteral("ntsc"), m_s.wide ? QStringLiteral("16:9") : QStringLiteral("4:3"),
                                       m_s.wide ? QStringLiteral(" widescreen=\"nopanscan\"") : QString());
        ts << "<dvdauthor dest=\"DVD\" jumppad=\"yes\">\n<vmgm>\n";
        ts << "<fpc>" << (m_s.playback == Settings::StartWithMenu ? "jump menu 1;" : "jump title 1;") << "</fpc>\n";
        ts << "<menus>" << video << "\n";
        for (int m = 0; m < m_menus.size(); ++m) {
            ts << "<pgc" << (m == 0 ? " entry=\"title\"" : "") << ">\n<vob file=\"" << m_menus[m].mpg << "\"/>\n";
            const int page = m - 1; // scenes page
            for (const MenuButton &b : std::as_const(m_menus[m].buttons)) {
                QString cmd;
                if (b.name == QLatin1String("PlayButton")) {
                    cmd = QStringLiteral("jump title 1;");
                } else if (b.name == QLatin1String("Submenu1Button")) {
                    cmd = QStringLiteral("jump menu 2;");
                } else if (b.name == QLatin1String("Submenu2Button")) {
                    cmd = notesMenu ? QStringLiteral("jump menu %1;").arg(notesMenu) : QStringLiteral("jump menu 1;");
                } else if (b.name.startsWith(QLatin1String("Scene"))) {
                    const int n = page * m_scenesPerPage + sceneNumber(b.name);
                    cmd = QStringLiteral("jump title %1;").arg(qMin(n, nTitles));
                } else if (b.name == QLatin1String("NextMenuButton")) {
                    cmd = QStringLiteral("jump menu %1;").arg(m + 2);
                } else if (b.name == QLatin1String("PreviousMenuButton")) {
                    cmd = QStringLiteral("jump menu %1;").arg(m);
                } else if (b.name == QLatin1String("ParentMenuButton")) {
                    cmd = QStringLiteral("jump menu 1;");
                } else {
                    cmd = QStringLiteral("jump menu 1;");
                }
                ts << "<button name=\"" << b.name << "\">" << cmd << "</button>\n";
            }
            ts << "<post>jump cell 1;</post>\n</pgc>\n";
        }
        ts << "</menus>\n</vmgm>\n<titleset>\n<menus>" << video << "<pgc><pre>jump vmgm menu 1;</pre></pgc></menus>\n<titles>" << video << "\n";
        for (int i = 0; i < nTitles; ++i) {
            // scenes every five minutes, as Windows DVD Maker splits long videos
            QStringList chapters{QStringLiteral("0")};
            for (int m = 5; m * 60 < titleSeconds(m_titles[i]) - 30; m += 5) {
                chapters << QStringLiteral("%1:%2:00").arg(m / 60).arg(m % 60, 2, 10, QLatin1Char('0'));
            }
            ts << "<pgc>\n<vob file=\"title" << i + 1 << ".mpg\" chapters=\"" << chapters.join(QLatin1Char(',')) << "\"/>\n<post>";
            if (i + 1 < nTitles) {
                ts << "jump title " << i + 2 << ";";
            } else if (m_s.playback == Settings::Loop) {
                ts << "jump title 1;";
            } else {
                ts << "call vmgm menu 1;";
            }
            ts << "</post>\n</pgc>\n";
        }
        ts << "</titles>\n</titleset>\n</dvdauthor>\n";
        ts.flush();
        QFile f(m_dir + QStringLiteral("/dvd.xml"));
        f.open(QIODevice::WriteOnly);
        f.write(x.toUtf8());
        f.close();
        qputenv("VIDEO_FORMAT", m_s.pal ? "PAL" : "NTSC");
        run(QStringLiteral("dvdauthor"), {QStringLiteral("-x"), QStringLiteral("dvd.xml")}, 74, 80, 0, tr7("Creating DVD, please wait..."));
    }

    void burn()
    {
        const QString iso = qEnvironmentVariable("SEVEN_DVDMAKER_ISO");
        if (m_device.isEmpty() && !iso.isEmpty()) {
            run(QStringLiteral("mkisofs"), {QStringLiteral("-dvd-video"), QStringLiteral("-udf"), QStringLiteral("-V"), QStringLiteral("DVD"),
                                             QStringLiteral("-o"), iso, QStringLiteral("DVD")},
                80, 100, 0, tr7("Burning DVD..."));
            return;
        }
        const QString label = m_s.texts.title.left(32).toUpper().replace(QRegularExpression(QStringLiteral("[^A-Z0-9_ ]")), QStringLiteral("_"));
        QStringList args{QStringLiteral("-dvd-compat")};
        if (m_s.speed > 0) {
            args << QStringLiteral("-speed=%1").arg(m_s.speed == 1 ? 4 : 2);
        }
        args << QStringLiteral("-Z") << m_device << QStringLiteral("-dvd-video") << QStringLiteral("-V") << (label.isEmpty() ? QStringLiteral("DVD") : label)
             << QStringLiteral("DVD");
        run(QStringLiteral("growisofs"), args, 80, 100, 0, tr7("Burning DVD..."));
    }

    QList<Item> m_items;
    Settings m_s;
    QString m_device;
    QString m_dir;
    QList<QList<Item>> m_titles;
    double m_total = 0;
    QList<std::function<void()>> m_steps;
    int m_step = -1;
    QProcess *m_proc = nullptr;
    bool m_cancelled = false;
    QElapsedTimer m_clock;
};
}
