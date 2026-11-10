/*
 * "AutoPlay" of Windows 7: the Control Panel page with one choice per media type, the background
 * watcher (seven-control --autoplay-watch, started at login) and the "AutoPlay" window that asks
 * what to do with a disc or USB drive.
 *
 *   new media           -> lsblk polled every 2 s (USB drives and discs only, never internal disks)
 *   content type        -> the files on it: pictures / video / music / mixed, VIDEO_TS, BDMV, MPEGAV...
 *   actions             -> Dolphin, Gwenview (view / import), Haruna, Elisa
 *   choices             -> ~/.config/seven-autoplayrc
 * Blank discs (no burning program installed), enhanced CDs/DVDs and DVD-Audio are left out.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"
#include "win7safelyremove.h"

#include "win7chrome.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDirIterator>
#include <QPushButton>
#include <QSettings>
#include <QTimer>

namespace Win7Autoplay
{
enum {
    AutoplayView = 42,
};

inline QSettings &store()
{
    static QSettings s(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/seven-autoplayrc"), QSettings::IniFormat);
    return s;
}

inline bool enabled()
{
    return store().value(QStringLiteral("General/Enabled"), true).toBool();
}

struct Action {
    QString key;
    QString title; // "Переглянути зображення"
    QString using_; // "використовуючи Gwenview"
    QString icon;
};

struct MediaType {
    QString key;
    QString title;
    QString icon;
    QStringList actions; // app actions, then "open", "nothing", "ask" are added
    QString always; // "Завжди виконувати для зображень:"
};

inline const QList<MediaType> &mediaTypes()
{
    static const QList<MediaType> t{
        {QStringLiteral("audiocd"), tr7("Audio CD"), QStringLiteral("media-optical-audio"), {QStringLiteral("haruna-cd")}, tr7("audio CDs")},
        {QStringLiteral("dvd"), tr7("DVD movie"), QStringLiteral("media-optical-video"), {QStringLiteral("haruna-dvd")}, tr7("DVD movies")},
        {QStringLiteral("programs"), tr7("Software and games"), QStringLiteral("applications-games"), {}, tr7("software and games")},
        {QStringLiteral("pictures"), tr7("Pictures"), QStringLiteral("image-x-generic"), {QStringLiteral("import"), QStringLiteral("photoviewer")}, tr7("pictures")},
        {QStringLiteral("video"), tr7("Video files"), QStringLiteral("video-x-generic"), {QStringLiteral("wmp"), QStringLiteral("import")}, tr7("video files")},
        {QStringLiteral("music"), tr7("Audio files"), QStringLiteral("audio-x-generic"), {QStringLiteral("wmp")}, tr7("audio files")},
        {QStringLiteral("mixed"), tr7("Mixed content"), QStringLiteral("drive-removable-media-usb"), {QStringLiteral("wmp"), QStringLiteral("import")}, tr7("mixed content")},
        {QStringLiteral("bluray"), tr7("Blu-ray disc movie"), QStringLiteral("media-optical-blu-ray"), {QStringLiteral("haruna-bd")}, tr7("Blu-ray movies")},
        {QStringLiteral("vcd"), QStringLiteral("Video CD"), QStringLiteral("media-optical-video"), {QStringLiteral("haruna-vcd")}, QStringLiteral("Video CD")},
    };
    return t;
}

inline Action action(const QString &key)
{
    static const QHash<QString, Action> a{
        {QStringLiteral("haruna-cd"), {QStringLiteral("haruna-cd"), tr7("Play audio CD"), tr7("using Haruna"), QStringLiteral("haruna")}},
        {QStringLiteral("haruna-dvd"), {QStringLiteral("haruna-dvd"), tr7("Play DVD movie"), tr7("using Haruna"), QStringLiteral("haruna")}},
        {QStringLiteral("haruna-bd"), {QStringLiteral("haruna-bd"), tr7("Play Blu-ray movie"), tr7("using Haruna"), QStringLiteral("haruna")}},
        {QStringLiteral("haruna-vcd"), {QStringLiteral("haruna-vcd"), tr7("Play Video CD"), tr7("using Haruna"), QStringLiteral("haruna")}},
        {QStringLiteral("haruna"), {QStringLiteral("haruna"), tr7("Play video"), tr7("using Haruna"), QStringLiteral("haruna")}},
        {QStringLiteral("elisa"), {QStringLiteral("elisa"), tr7("Play music"), tr7("using Elisa"), QStringLiteral("elisa")}},
        {QStringLiteral("gwenview"), {QStringLiteral("gwenview"), tr7("View pictures"), tr7("using Gwenview"), QStringLiteral("gwenview")}},
        {QStringLiteral("import"), {QStringLiteral("import"), tr7("Import pictures and videos"), tr7("using Windows"), QStringLiteral("camera-photo")}},
        {QStringLiteral("wmp"), {QStringLiteral("wmp"), tr7("Play"), tr7("using Windows Media Player"), QStringLiteral("seven-wmp")}},
        {QStringLiteral("photoviewer"), {QStringLiteral("photoviewer"), tr7("View pictures"), tr7("using Windows Photo Viewer"), QStringLiteral("seven-photoviewer")}},
        {QStringLiteral("open"), {QStringLiteral("open"), tr7("Open folder to view files"), tr7("using Windows Explorer"), QStringLiteral("seven-bk-folder")}},
        {QStringLiteral("backup"), {QStringLiteral("backup"), tr7("Use this drive for backup"), tr7("using Windows Backup"), QStringLiteral("seven-cpi-backup")}},
        {QStringLiteral("nothing"), {QStringLiteral("nothing"), tr7("Take no action"), QString(), QStringLiteral("dialog-cancel")}},
        {QStringLiteral("ask"), {QStringLiteral("ask"), tr7("Ask me every time"), QString(), QStringLiteral("dialog-question")}},
    };
    return a.value(key);
}

inline QStringList choicesFor(const MediaType &t)
{
    QStringList c = t.actions;
    if (t.key != QLatin1String("audiocd")) {
        c << QStringLiteral("open");
    }
    c << QStringLiteral("nothing") << QStringLiteral("ask");
    return c;
}

inline QString handler(const QString &type)
{
    return store().value(QStringLiteral("Handlers/") + type).toString(); // empty = "default" = ask
}

/* ---------------- Control Panel page ---------------- */

struct Nav {
    std::function<void()> back;
};

inline QWidget *buildPage(const Nav &nav)
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("win7page"));
    page->setAttribute(Qt::WA_StyledBackground, true);
    page->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *body = new QWidget;
    auto *v = new QVBoxLayout(body);
    v->setContentsMargins(30, 14, 30, 14);
    v->setSpacing(6);
    auto *title = new QLabel(tr7("Choose what happens when you insert each type of media or device"));
    QFont tf = title->font();
    tf.setPixelSize(16);
    title->setFont(tf);
    title->setStyleSheet(QStringLiteral("color: #1e3287;"));
    title->setWordWrap(true);
    v->addWidget(title);
    auto *use = new QCheckBox(tr7("Use AutoPlay for all media and devices"));
    use->setChecked(enabled());
    v->addWidget(use);
    v->addSpacing(8);
    v->addWidget(new SectionHeader(tr7("Media")));
    v->addSpacing(4);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);
    QList<QComboBox *> combos;
    QWidget *rows = new QWidget;
    rows->setLayout(grid);
    int r = 0;
    for (const MediaType &t : mediaTypes()) {
        if (r == 2 || r == 3 || r == 6 || r == 7) {
            grid->setRowMinimumHeight(r * 2 - 1, 14); // the gaps between the groups of media in Windows 7
        }
        auto *icon = new QLabel;
        icon->setPixmap(QIcon::fromTheme(t.icon).pixmap(16, 16));
        auto *label = new QLabel(t.title);
        auto *combo = new QComboBox;
        combo->setFixedWidth(370);
        combo->setIconSize(QSize(16, 16));
        combo->setPlaceholderText(tr7("Default"));
        for (const QString &k : choicesFor(t)) {
            const Action a = action(k);
            combo->addItem(QIcon::fromTheme(a.icon), a.using_.isEmpty() ? a.title : a.title + QStringLiteral(", ") + a.using_, k);
        }
        combo->setCurrentIndex(combo->findData(handler(t.key)));
        combo->setProperty("type", t.key);
        combos << combo;
        grid->addWidget(icon, r * 2, 0);
        grid->addWidget(label, r * 2, 1);
        grid->addWidget(combo, r * 2, 2, Qt::AlignLeft);
        ++r;
    }
    grid->setColumnMinimumWidth(1, 210);
    grid->setColumnStretch(3, 1);
    v->addWidget(rows);
    v->addSpacing(10);
    auto *reset = new QPushButton(tr7("Reset all defaults"));
    v->addWidget(reset, 0, Qt::AlignLeft);
    v->addStretch(1);
    auto setEnabled = [rows](bool on) {
        rows->setEnabled(on);
    };
    setEnabled(use->isChecked());
    QObject::connect(use, &QCheckBox::toggled, rows, setEnabled);
    QObject::connect(reset, &QPushButton::clicked, rows, [combos] {
        for (QComboBox *c : combos) {
            c->setCurrentIndex(-1);
        }
    });
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(body);
    body->setObjectName(QStringLiteral("win7apbody"));
    body->setAttribute(Qt::WA_StyledBackground, true);
    body->setStyleSheet(QStringLiteral("#win7apbody { background: #ffffff; }"));
    outer->addWidget(scroll, 1);

    // "Save" / "Cancel" stay at the bottom like in Windows 7
    auto *bar = new QWidget;
    bar->setObjectName(QStringLiteral("win7apbar"));
    bar->setAttribute(Qt::WA_StyledBackground, true);
    bar->setStyleSheet(QStringLiteral("#win7apbar { background: #ffffff; border-top: 1px solid #dfdfdf; }"));
    auto *bh = new QHBoxLayout(bar);
    bh->setContentsMargins(10, 8, 14, 8);
    bh->addStretch(1);
    auto *save = new QPushButton(tr7("Save"));
    auto *cancel = new QPushButton(tr7("Cancel"));
    save->setMinimumWidth(75);
    cancel->setMinimumWidth(75);
    bh->addWidget(save);
    bh->addWidget(cancel);
    outer->addWidget(bar);
    QObject::connect(save, &QPushButton::clicked, page, [use, combos, nav] {
        store().setValue(QStringLiteral("General/Enabled"), use->isChecked());
        for (QComboBox *c : combos) {
            const QString key = QStringLiteral("Handlers/") + c->property("type").toString();
            if (c->currentIndex() < 0) {
                store().remove(key);
            } else {
                store().setValue(key, c->currentData().toString());
            }
        }
        store().sync();
        nav.back();
    });
    QObject::connect(cancel, &QPushButton::clicked, page, [nav] {
        nav.back();
    });
    return page;
}

/* ---------------- what is on the media ---------------- */

struct Media {
    QString device; // /dev/sdb1, /dev/sr0
    QString label;
    QString mountPoint;
    QString type; // MediaType key
    bool optical = false;
};

inline QString classify(const QString &root)
{
    const QDir d(root);
    auto has = [&d](const QString &name) {
        return d.exists(name) || d.exists(name.toLower());
    };
    if (has(QStringLiteral("VIDEO_TS"))) {
        return QStringLiteral("dvd");
    }
    if (has(QStringLiteral("BDMV"))) {
        return QStringLiteral("bluray");
    }
    if (has(QStringLiteral("MPEGAV")) || has(QStringLiteral("MPEG2"))) {
        return QStringLiteral("vcd");
    }
    if (has(QStringLiteral("autorun.inf")) || has(QStringLiteral("AUTORUN.INF")) || has(QStringLiteral("setup.exe"))) {
        return QStringLiteral("programs");
    }
    static const QStringList pics{QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("heic"), QStringLiteral("webp"), QStringLiteral("cr2"), QStringLiteral("nef"), QStringLiteral("dng")};
    static const QStringList vids{QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"), QStringLiteral("mov"), QStringLiteral("wmv"), QStringLiteral("webm"), QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("m4v"), QStringLiteral("3gp"), QStringLiteral("mts")};
    static const QStringList auds{QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("wav"), QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("wma"), QStringLiteral("opus")};
    int p = 0, v = 0, a = 0, other = 0, n = 0;
    QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext() && n < 2000) {
        it.next();
        ++n;
        const QString ext = it.fileInfo().suffix().toLower();
        if (pics.contains(ext)) {
            ++p;
        } else if (vids.contains(ext)) {
            ++v;
        } else if (auds.contains(ext)) {
            ++a;
        } else {
            ++other;
        }
    }
    const int kinds = (p > 0) + (v > 0) + (a > 0);
    if (kinds == 1 && other <= (p + v + a) / 4) {
        return p ? QStringLiteral("pictures") : v ? QStringLiteral("video") : QStringLiteral("music");
    }
    if (p == 0 && v == 0 && a == 0 && n > 0) {
        return QStringLiteral("mixed");
    }
    // pictures with a few videos from a camera are still "pictures" in Windows 7
    if (p > 0 && v > 0 && a == 0 && p >= v * 3) {
        return QStringLiteral("pictures");
    }
    return QStringLiteral("mixed");
}

inline QStringList filesOf(const QString &root, const QStringList &exts, int max = 300)
{
    QStringList files;
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext() && files.size() < max) {
        const QString f = it.next();
        if (exts.contains(it.fileInfo().suffix().toLower())) {
            files << f;
        }
    }
    files.sort();
    return files;
}

inline QString mountIfNeeded(Media &m)
{
    if (m.mountPoint.isEmpty() && !m.device.isEmpty()) {
        QProcess p;
        p.start(QStringLiteral("udisksctl"), {QStringLiteral("mount"), QStringLiteral("--no-user-interaction"), QStringLiteral("-b"), m.device});
        p.waitForFinished(20000);
        static const QRegularExpression at(QStringLiteral(" at (.+?)\\.?\\s*$"));
        m.mountPoint = at.match(QString::fromUtf8(p.readAllStandardOutput()).trimmed()).captured(1);
    }
    return m.mountPoint;
}

inline void runAction(const QString &key, Media m)
{
    const QString mp = (key == QLatin1String("haruna-cd") || key == QLatin1String("nothing")) ? QString() : mountIfNeeded(m);
    if (key == QLatin1String("open")) {
        launch(QStringLiteral("dolphin"), {mp});
    } else if (key == QLatin1String("gwenview")) {
        launch(QStringLiteral("gwenview"), {mp});
    } else if (key == QLatin1String("import")) {
        launch(QStringLiteral("gwenview_importer"), {mp});
    } else if (key == QLatin1String("haruna")) {
        const QStringList v = filesOf(mp, {QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"), QStringLiteral("mov"), QStringLiteral("wmv"), QStringLiteral("webm"), QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("m4v"), QStringLiteral("3gp"), QStringLiteral("mts")}, 1);
        launch(QStringLiteral("haruna"), {v.isEmpty() ? mp : v.first()});
    } else if (key == QLatin1String("elisa")) {
        launch(QStringLiteral("elisa"), filesOf(mp, {QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("wav"), QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("wma"), QStringLiteral("opus")}));
    } else if (key == QLatin1String("wmp")) {
        // Windows Media Player gets the music and videos of the drive as its Now Playing list
        const QStringList f = filesOf(mp, {QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("wav"), QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("wma"), QStringLiteral("opus"),
                                            QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"), QStringLiteral("mov"), QStringLiteral("wmv"), QStringLiteral("webm"), QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("m4v"), QStringLiteral("3gp")});
        launch(QStringLiteral("seven-wmp"), f.isEmpty() ? QStringList{mp} : f);
    } else if (key == QLatin1String("photoviewer")) {
        const QStringList f = filesOf(mp, {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("tif"), QStringLiteral("tiff")}, 1);
        launch(QStringLiteral("seven-photoviewer"), {f.isEmpty() ? mp : f.first()});
    } else if (key == QLatin1String("backup")) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--backup")});
    } else if (key == QLatin1String("haruna-cd")) {
        launch(QStringLiteral("haruna"), {QStringLiteral("cdda://")});
    } else if (key == QLatin1String("haruna-dvd")) {
        launch(QStringLiteral("haruna"), {QStringLiteral("dvd://")});
    } else if (key == QLatin1String("haruna-bd")) {
        launch(QStringLiteral("haruna"), {QStringLiteral("bd://")});
    } else if (key == QLatin1String("haruna-vcd")) {
        launch(QStringLiteral("haruna"), {QStringLiteral("vcd://")});
    }
}

/* ---------------- the "AutoPlay" window ---------------- */

/** Big option row: 32 px icon, the action and grey "using <program>" */
class OptionRow : public QWidget
{
public:
    OptionRow(const Action &a, std::function<void()> activate, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_a(a)
        , m_activate(std::move(activate))
    {
        setAttribute(Qt::WA_Hover);
        setFixedHeight(44);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (underMouse()) {
            QLinearGradient g(0, 0, 0, height());
            g.setColorAt(0, QColor(0xfa, 0xfb, 0xfd));
            g.setColorAt(1, QColor(0xeb, 0xf3, 0xfd));
            p.setPen(QColor(0xb8, 0xd6, 0xfb));
            p.setBrush(g);
            p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        }
        p.drawPixmap(8, 6, QIcon::fromTheme(m_a.icon).pixmap(32, 32));
        p.setPen(QColor(0x15, 0x1c, 0x55));
        const bool two = !m_a.using_.isEmpty();
        p.drawText(QRect(50, two ? 5 : 0, width() - 54, two ? 17 : height()), Qt::AlignLeft | Qt::AlignVCenter, m_a.title);
        if (two) {
            p.setPen(QColor(0x6d, 0x6d, 0x6d));
            p.drawText(QRect(50, 22, width() - 54, 17), Qt::AlignLeft | Qt::AlignVCenter, m_a.using_);
        }
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && m_activate) {
            m_activate();
        }
    }

private:
    Action m_a;
    std::function<void()> m_activate;
};

inline void askDialog(const Media &m)
{
    MediaType t;
    for (const MediaType &x : mediaTypes()) {
        if (x.key == m.type) {
            t = x;
        }
    }
    auto *dlg = new QDialog;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr7("AutoPlay"));
    dlg->setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-autoplay")));
    dlg->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowStaysOnTopHint);
    dlg->setFixedWidth(380);
    auto *v = new QVBoxLayout(dlg);
    v->setContentsMargins(0, 0, 0, 10);
    v->setSpacing(0);
    auto *white = new QWidget;
    white->setObjectName(QStringLiteral("win7apwhite"));
    white->setAttribute(Qt::WA_StyledBackground, true);
    white->setStyleSheet(QStringLiteral("#win7apwhite { background: #ffffff; }"));
    auto *wv = new QVBoxLayout(white);
    wv->setContentsMargins(12, 12, 12, 10);
    auto *head = new QHBoxLayout;
    auto *icon = new QLabel;
    icon->setPixmap(QIcon::fromTheme(m.optical ? t.icon : QStringLiteral("drive-removable-media-usb")).pixmap(48, 48));
    head->addWidget(icon);
    head->addSpacing(8);
    QString name = m.label.isEmpty() ? (m.optical ? tr7("DVD RW Drive") : tr7("Removable Disk")) : m.label;
    auto *title = new QLabel(QStringLiteral("%1 (%2)").arg(name, m.device.section(QLatin1Char('/'), -1)));
    QFont f = title->font();
    f.setPixelSize(15);
    title->setFont(f);
    head->addWidget(title, 1);
    wv->addLayout(head);
    wv->addSpacing(8);
    auto *always = new QCheckBox(tr7("Always do this for %1:").arg(t.always));
    wv->addWidget(always);
    // Windows 7 has no "always do this" for drives with mixed content
    always->setVisible(m.type != QLatin1String("mixed"));
    wv->addSpacing(6);
    auto choose = [dlg, always, m](const QString &key) {
        if (always->isChecked()) {
            store().setValue(QStringLiteral("Handlers/") + m.type, key);
            store().sync();
        }
        dlg->close();
        runAction(key, m);
    };
    QStringList apps = t.actions;
    if (!apps.isEmpty()) {
        auto *sh = new SectionHeader(tr7("%1 settings").arg(t.always));
        sh->setBlue(true);
        wv->addWidget(sh);
        for (const QString &k : apps) {
            wv->addWidget(new OptionRow(action(k), [choose, k] {
                choose(k);
            }));
        }
    }
    if (m.type != QLatin1String("audiocd")) {
        wv->addSpacing(4);
        auto *gh = new SectionHeader(tr7("General options"));
        gh->setBlue(true);
        wv->addWidget(gh);
        wv->addWidget(new OptionRow(action(QStringLiteral("open")), [choose] {
            choose(QStringLiteral("open"));
        }));
        if (!m.optical) {
            // Windows 7 offers removable drives to Windows Backup
            wv->addWidget(new OptionRow(action(QStringLiteral("backup")), [dlg, m] {
                dlg->close();
                runAction(QStringLiteral("backup"), m);
            }));
        }
    }
    wv->addSpacing(10);
    wv->addWidget(new Link(tr7("View more AutoPlay options in Control Panel"), [dlg] {
        dlg->close();
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--autoplay")});
    }));
    v->addWidget(white);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

/** Decide for new media: the saved choice, or the window */
inline void handle(Media m)
{
    if (!enabled()) {
        return;
    }
    if (m.type.isEmpty()) {
        bool anyAction = false;
        for (const MediaType &t : mediaTypes()) {
            anyAction = anyAction || handler(t.key) != QLatin1String("nothing");
        }
        if (!anyAction || mountIfNeeded(m).isEmpty()) {
            return;
        }
        m.type = classify(m.mountPoint);
    }
    const QString h = handler(m.type);
    if (h == QLatin1String("nothing")) {
        return;
    }
    if (h.isEmpty() || h == QLatin1String("ask")) {
        askDialog(m);
    } else {
        runAction(h, m);
    }
}

/* ---------------- the watcher ---------------- */

/** USB partitions with a file system and discs in the drive: key -> media */
inline QMap<QString, Media> currentMedia()
{
    QMap<QString, Media> map;
    QProcess p;
    p.start(QStringLiteral("lsblk"), {QStringLiteral("-P"), QStringLiteral("-p"), QStringLiteral("-o"), QStringLiteral("NAME,PKNAME,TYPE,TRAN,HOTPLUG,FSTYPE,LABEL,MOUNTPOINT,SIZE")});
    p.waitForFinished(3000);
    static const QRegularExpression kv(QStringLiteral("(\\w+)=\"([^\"]*)\""));
    QHash<QString, QHash<QString, QString>> rows;
    QStringList order;
    for (const QString &line : QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        QHash<QString, QString> r;
        for (auto it = kv.globalMatch(line); it.hasNext();) {
            const auto m = it.next();
            r.insert(m.captured(1), m.captured(2));
        }
        rows.insert(r.value(QStringLiteral("NAME")), r);
        order << r.value(QStringLiteral("NAME"));
    }
    // the drive Linux runs from is a USB drive too: never treat it as removable media
    QSet<QString> systemDisks;
    for (const QString &name : std::as_const(order)) {
        const auto r = rows.value(name);
        static const QStringList sys{QStringLiteral("/"), QStringLiteral("/boot"), QStringLiteral("/boot/efi"), QStringLiteral("/home"), QStringLiteral("[SWAP]")};
        if (sys.contains(r.value(QStringLiteral("MOUNTPOINT")))) {
            systemDisks.insert(r.value(QStringLiteral("PKNAME")).isEmpty() ? name : r.value(QStringLiteral("PKNAME")));
        }
    }
    for (const QString &name : std::as_const(order)) {
        const auto r = rows.value(name);
        if (systemDisks.contains(name) || systemDisks.contains(r.value(QStringLiteral("PKNAME")))) {
            continue;
        }
        const auto parent = rows.value(r.value(QStringLiteral("PKNAME")));
        const QString type = r.value(QStringLiteral("TYPE"));
        const bool usb = r.value(QStringLiteral("TRAN")) == QLatin1String("usb") || parent.value(QStringLiteral("TRAN")) == QLatin1String("usb");
        const bool hotplug = r.value(QStringLiteral("HOTPLUG")) == QLatin1String("1") || parent.value(QStringLiteral("HOTPLUG")) == QLatin1String("1");
        if (type == QLatin1String("rom")) {
            const QString size = r.value(QStringLiteral("SIZE"));
            if (size.isEmpty() || size == QLatin1String("0B") || size == QLatin1String("1024M")) {
                continue; // no disc
            }
            Media m;
            m.device = name;
            m.optical = true;
            m.label = r.value(QStringLiteral("LABEL"));
            m.mountPoint = r.value(QStringLiteral("MOUNTPOINT"));
            if (r.value(QStringLiteral("FSTYPE")).isEmpty()) {
                QProcess u;
                u.start(QStringLiteral("udevadm"), {QStringLiteral("info"), QStringLiteral("--query=property"), name});
                u.waitForFinished(2000);
                const QString props = QString::fromUtf8(u.readAllStandardOutput());
                if (!props.contains(QLatin1String("ID_CDROM_MEDIA_TRACK_COUNT_AUDIO"))) {
                    continue; // blank disc
                }
                m.type = QStringLiteral("audiocd");
            }
            map.insert(name + size, m);
            continue;
        }
        if ((type != QLatin1String("part") && type != QLatin1String("disk")) || r.value(QStringLiteral("FSTYPE")).isEmpty() || !(usb || hotplug)) {
            continue;
        }
        if (name.contains(QLatin1String("nvme"))) {
            continue; // never touch the internal disks
        }
        Media m;
        m.device = name;
        m.label = r.value(QStringLiteral("LABEL"));
        m.mountPoint = r.value(QStringLiteral("MOUNTPOINT"));
        map.insert(name + r.value(QStringLiteral("FSTYPE")) + r.value(QStringLiteral("SIZE")), m);
    }
    return map;
}

inline int watch(QApplication &app)
{
    app.setQuitOnLastWindowClosed(false);
    new Win7SafelyRemove::Tray(&app); // the "Safely Remove Hardware" icon lives in this watcher too
    new Win7SafelyRemove::SpeedWatcher(&app); // "This device can perform faster"
    auto known = std::make_shared<QMap<QString, Media>>(currentMedia());
    auto *timer = new QTimer(&app);
    QObject::connect(timer, &QTimer::timeout, &app, [known] {
        const QMap<QString, Media> now = currentMedia();
        for (auto it = now.cbegin(); it != now.cend(); ++it) {
            if (!known->contains(it.key())) {
                handle(it.value());
            }
        }
        *known = now;
    });
    timer->start(2000);
    return app.exec();
}
}
