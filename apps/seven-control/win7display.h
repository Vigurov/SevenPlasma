/*
 * "Personalization", "Display" and "Screen Resolution" pages of the Windows 7 style Control Panel.
 * Themes are Plasma global themes, the display settings go through kscreen-doctor,
 * text size is the Plasma global scale (applied on the next login, like in Windows 7).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7kwin.h"
#include "win7windowclass.h"
#include "win7i18n.h"

#include "win7chrome.h"
#include "win7sound.h"
#include "win7screensaver.h"

#include <KConfig>
#include <KConfigGroup>

#include <QButtonGroup>
#include <QComboBox>
#include <QDBusInterface>
#include <QDBusReply>
#include <QCheckBox>
#include <QDialog>
#include <QDirIterator>
#include <KDesktopFile>
#include <KIconDialog>
#include <QSettings>
#include <QStandardPaths>
#include <QDialogButtonBox>
#include <QDir>
#include <QInputDialog>
#include <QPainterPath>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QTabWidget>
#include <QTimer>

namespace Win7Display
{
enum {
    PersonalizeView = 80,
    DisplayView = 81,
    ResolutionView = 82,
};

struct Nav {
    std::function<void(int)> open;
    std::function<void()> home;
    std::function<void()> back;
    std::function<void()> powerPlans;
};

inline QString run(const QString &exe, const QStringList &args, int timeoutMs = 5000)
{
    QProcess p;
    p.start(exe, args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        return {};
    }
    return QString::fromUtf8(p.readAllStandardOutput());
}

inline void systemSettings(const QString &kcm)
{
    launch(QStringLiteral("systemsettings"), {kcm});
}

inline void openWindowColor()
{
    launch(QStringLiteral("kcmshell6"), {QStringLiteral("kwin/effects/configs/kwin_aeroglassblur_config")});
}

/** Win+P display switcher of KDE (the "Connect to a projector" of Windows 7) */
inline void showDisplaySwitcher()
{
    QProcess::startDetached(QStringLiteral("qdbus6"), {QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/component/org_kde_kscreen_desktop"),
                                                       QStringLiteral("org.kde.kglobalaccel.Component.invokeShortcut"), QStringLiteral("ShowOSD")});
}

inline QLabel *pageTitle(const QString &text)
{
    auto *l = new QLabel(text);
    QFont f = l->font();
    f.setPixelSize(16);
    l->setFont(f);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color: #1e3287;"));
    return l;
}

inline QLabel *plain(const QString &text)
{
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    return l;
}

/* ---------------- left pane shared by the pages ---------------- */

struct SideItem {
    QString text;
    std::function<void()> open;
    QString icon;
    bool shield = false;
};

inline QWidget *sidePane(const std::function<void()> &home, const QList<SideItem> &items, const QList<SideItem> &seeAlso)
{
    auto *side = new QWidget;
    side->setObjectName(QStringLiteral("win7side"));
    side->setAttribute(Qt::WA_StyledBackground, true);
    side->setFixedWidth(200);
    auto *sv = new QVBoxLayout(side);
    sv->setContentsMargins(4, 16, 10, 14);
    sv->setSpacing(7);
    const QString color = QStringLiteral("#1e3a5f");
    auto addRow = [sv, color](const SideItem &it) {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        auto *ic = new QLabel;
        ic->setFixedWidth(18);
        if (it.shield) {
            ic->setPixmap(QIcon::fromTheme(QStringLiteral("seven-cp-shield")).pixmap(16, 16));
        } else if (!it.icon.isEmpty()) {
            ic->setPixmap(QIcon::fromTheme(it.icon).pixmap(16, 16));
        }
        ic->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        row->addWidget(ic, 0, Qt::AlignTop);
        row->addWidget(new Link(it.text, it.open, color), 1);
        sv->addLayout(row);
    };
    addRow({tr7("Control Panel Home"), home});
    sv->addSpacing(9);
    for (const SideItem &it : items) {
        addRow(it);
    }
    sv->addStretch(1);
    if (!seeAlso.isEmpty()) {
        auto *seeRow = new QHBoxLayout;
        seeRow->setContentsMargins(22, 0, 0, 0);
        seeRow->addWidget(grey(tr7("See also")));
        sv->addLayout(seeRow);
    }
    for (const SideItem &it : seeAlso) {
        addRow(it);
    }
    return side;
}

inline QWidget *pageWithSide(QWidget *side, QWidget *content)
{
    auto *root = new QWidget;
    root->setObjectName(QStringLiteral("win7page"));
    root->setAttribute(Qt::WA_StyledBackground, true);
    root->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"
                                       "#win7side { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #e3ecf8, stop:0.5 #eef3fb, stop:1 #f3f7fc);"
                                       " border-right: 1px solid #e0e8f3; }"));
    auto *h = new QHBoxLayout(root);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    // Windows 7: a short window cuts the side pane off at the bottom instead of growing for it
    side->setSizePolicy(side->sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
    h->addWidget(side);
    h->addWidget(content, 1);
    return root;
}


/* ---------------- "Desktop Icon Settings" ---------------- */

/** One of the special desktop icons: a .desktop link on ~/Desktop */
struct DeskIcon {
    QString key;       // config key / file name part
    QString title;     // checkbox text
    QString name;      // name under the icon on the desktop
    QString url;       // Type=Link target, or empty for Control Panel
    QString exec;      // Type=Application command
    QString defIcon;
    QString defEmptyIcon; // Recycle Bin only
};

inline QList<DeskIcon> deskIcons()
{
    const QString home = QDir::homePath();
    const QString user = QString::fromLocal8Bit(qgetenv("USER"));
    return {
        // computer:/ only exists inside our Explorer, so the desktop icon starts it with that page
        {QStringLiteral("computer"), tr7("Computer"), tr7("Computer"), QStringLiteral("computer:/"), QStringLiteral("dolphin computer:/"), QStringLiteral("seven-desk-computer"), {}},
        {QStringLiteral("user"), tr7("User's Files"), user, QUrl::fromLocalFile(home).toString(), {}, QStringLiteral("seven-desk-user"), {}},
        {QStringLiteral("network"), tr7("Network"), tr7("Network"), QStringLiteral("remote:/"), {}, QStringLiteral("seven-desk-network"), {}},
        {QStringLiteral("trash"), tr7("Recycle Bin"), tr7("Recycle Bin"), QStringLiteral("trash:/"), {}, QStringLiteral("seven-desk-trash-full"), QStringLiteral("seven-desk-trash-empty")},
        {QStringLiteral("control"), tr7("Control Panel"), tr7("Control Panel"), {}, home + QStringLiteral("/.local/bin/seven-control"), QStringLiteral("seven-desk-control"), {}},
    };
}

inline QString desktopDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
}

/** The existing file on the desktop for this icon (also ones KDE created, e.g. the Trash link) */
inline QString findDeskFile(const DeskIcon &d)
{
    QDirIterator it(desktopDir(), {QStringLiteral("*.desktop")}, QDir::Files | QDir::Hidden);
    while (it.hasNext()) {
        const QString f = it.next();
        KDesktopFile df(f);
        KConfigGroup g = df.desktopGroup();
        if (!d.url.isEmpty() && g.readEntry("Type") == QLatin1String("Link")) {
            QString u = g.readEntry("URL");
            if (u == d.url || u == d.url + QLatin1Char('/') || (d.key == QLatin1String("trash") && u.startsWith(QLatin1String("trash:")))) {
                return f;
            }
        }
        if (!d.exec.isEmpty() && g.readEntry("Exec") == d.exec) {
            return f;
        }
    }
    return {};
}

inline QSettings &deskStore()
{
    static QSettings s(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/seven-deskiconsrc"), QSettings::IniFormat);
    return s;
}

inline void writeDeskFile(const DeskIcon &d, const QString &icon, const QString &emptyIcon)
{
    QString file = findDeskFile(d);
    if (file.isEmpty()) {
        file = desktopDir() + QStringLiteral("/seven-%1.desktop").arg(d.key);
    }
    KDesktopFile df(file);
    KConfigGroup g = df.desktopGroup();
    g.writeEntry("Name", d.name);
    g.writeEntry("Icon", icon);
    if (!d.exec.isEmpty()) {
        g.writeEntry("Type", "Application");
        g.writeEntry("Exec", d.exec);
        g.deleteEntry("URL");
    } else {
        g.writeEntry("Type", "Link");
        g.writeEntry("URL", d.url);
        if (!emptyIcon.isEmpty()) {
            g.writeEntry("EmptyIcon", emptyIcon);
        }
    }
    df.sync();
    QFile::setPermissions(file, QFile::permissions(file) | QFileDevice::ExeOwner | QFileDevice::ExeUser);
}

/** Icon in the preview list: picture with the caption below, blue selection like Explorer */
inline void desktopIconsDialog(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr7("Desktop Icon Settings"));
    dlg.setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-personalize")));
    auto *v = new QVBoxLayout(&dlg);
    auto *tabs = new QTabWidget;
    auto *page = new QWidget;
    auto *pv = new QVBoxLayout(page);

    const QList<DeskIcon> icons = deskIcons();
    auto *box = new QGroupBox(tr7("Desktop icons"));
    auto *grid = new QGridLayout(box);
    grid->setHorizontalSpacing(40);
    QMap<QString, QCheckBox *> checks;
    // Windows 7 layout: Computer, User's Files, Network in the first column; Recycle Bin, Control Panel in the second
    const QList<std::tuple<QString, int, int>> place{{QStringLiteral("computer"), 0, 0}, {QStringLiteral("user"), 1, 0}, {QStringLiteral("network"), 2, 0},
                                                     {QStringLiteral("trash"), 0, 1}, {QStringLiteral("control"), 1, 1}};
    for (const auto &[key, row, col] : place) {
        for (const DeskIcon &d : icons) {
            if (d.key == key) {
                auto *c = new QCheckBox(d.title);
                c->setChecked(!findDeskFile(d).isEmpty());
                checks.insert(key, c);
                grid->addWidget(c, row, col);
            }
        }
    }
    grid->setColumnStretch(2, 1);
    pv->addWidget(box);

    // preview list of icons that can be changed
    struct Entry {
        QString key;
        bool empty; // Recycle Bin (empty)
        QString caption;
    };
    const QString user = QString::fromLocal8Bit(qgetenv("USER"));
    const QList<Entry> entries{{QStringLiteral("computer"), false, tr7("Computer")},
                               {QStringLiteral("user"), false, user},
                               {QStringLiteral("network"), false, tr7("Network")},
                               {QStringLiteral("trash"), false, tr7("Recycle Bin\n(full)")},
                               {QStringLiteral("trash"), true, tr7("Recycle Bin\n(empty)")}};
    auto iconFor = [icons](const Entry &e) {
        for (const DeskIcon &d : icons) {
            if (d.key == e.key) {
                const QString k = e.empty ? d.key + QStringLiteral("/EmptyIcon") : d.key + QStringLiteral("/Icon");
                return deskStore().value(k, e.empty ? d.defEmptyIcon : d.defIcon).toString();
            }
        }
        return QString();
    };
    auto defaultFor = [icons](const Entry &e) {
        for (const DeskIcon &d : icons) {
            if (d.key == e.key) {
                return e.empty ? d.defEmptyIcon : d.defIcon;
            }
        }
        return QString();
    };
    auto chosen = std::make_shared<QMap<int, QString>>();
    for (int i = 0; i < entries.size(); ++i) {
        chosen->insert(i, iconFor(entries.at(i)));
    }
    auto *list = new QListWidget;
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(48, 48));
    list->setGridSize(QSize(92, 92));
    list->setMovement(QListView::Static);
    list->setResizeMode(QListView::Adjust);
    list->setWordWrap(true);
    list->setMinimumHeight(200);
    list->setStyleSheet(QStringLiteral("QListWidget { border: 1px solid #828790; background: #ffffff; }"));
    for (int i = 0; i < entries.size(); ++i) {
        auto *item = new QListWidgetItem(QIcon::fromTheme(chosen->value(i)), entries.at(i).caption, list);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
    }
    list->setCurrentRow(0);
    pv->addWidget(list, 1);
    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    auto *change = new QPushButton(tr7("Change Icon..."));
    auto *restore = new QPushButton(tr7("Restore Default"));
    change->setAutoDefault(false);
    restore->setAutoDefault(false);
    btnRow->addWidget(change);
    btnRow->addWidget(restore);
    pv->addLayout(btnRow);
    tabs->addTab(page, tr7("Desktop icons"));
    v->addWidget(tabs);

    auto *buttons = new QDialogButtonBox;
    auto *ok = buttons->addButton(QStringLiteral("OK"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr7("Cancel"), QDialogButtonBox::RejectRole);
    auto *apply = buttons->addButton(tr7("Apply"), QDialogButtonBox::ApplyRole);
    for (QAbstractButton *b : buttons->buttons()) {
        b->setMinimumWidth(80);
    }
    apply->setEnabled(false);
    ok->setDefault(true);
    v->addWidget(buttons);
    dlg.resize(470, 520);

    auto dirty = [apply] {
        apply->setEnabled(true);
    };
    for (QCheckBox *c : std::as_const(checks)) {
        QObject::connect(c, &QCheckBox::toggled, &dlg, dirty);
    }
    auto refreshRestore = [=] {
        const int row = list->currentRow();
        restore->setEnabled(row >= 0 && chosen->value(row) != defaultFor(entries.at(row)));
    };
    QObject::connect(list, &QListWidget::currentRowChanged, &dlg, refreshRestore);
    refreshRestore();
    auto setIcon = [=](const QString &name) {
        const int row = list->currentRow();
        if (row < 0 || name.isEmpty()) {
            return;
        }
        chosen->insert(row, name);
        list->item(row)->setIcon(QIcon::fromTheme(name, QIcon(name)));
        refreshRestore();
        dirty();
    };
    QObject::connect(change, &QPushButton::clicked, &dlg, [=] {
        setIcon(KIconDialog::getIcon(KIconLoader::Desktop, KIconLoader::Any, false, 48, false, list));
    });
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dlg, [change] {
        change->click();
    });
    QObject::connect(restore, &QPushButton::clicked, &dlg, [=] {
        const int row = list->currentRow();
        if (row >= 0) {
            setIcon(defaultFor(entries.at(row)));
        }
    });

    auto save = [=] {
        for (int i = 0; i < entries.size(); ++i) {
            const Entry &e = entries.at(i);
            deskStore().setValue(e.empty ? e.key + QStringLiteral("/EmptyIcon") : e.key + QStringLiteral("/Icon"), chosen->value(i));
        }
        deskStore().sync();
        for (const DeskIcon &d : icons) {
            const QString icon = deskStore().value(d.key + QStringLiteral("/Icon"), d.defIcon).toString();
            const QString emptyIcon = d.defEmptyIcon.isEmpty() ? QString() : deskStore().value(d.key + QStringLiteral("/EmptyIcon"), d.defEmptyIcon).toString();
            const QString existing = findDeskFile(d);
            if (checks.value(d.key)->isChecked()) {
                writeDeskFile(d, icon, emptyIcon);
            } else if (!existing.isEmpty()) {
                QFile::remove(existing);
            }
        }
        apply->setEnabled(false);
    };
    QObject::connect(buttons, &QDialogButtonBox::clicked, &dlg, [=, &dlg](QAbstractButton *b) {
        if (b == apply) {
            save();
        } else if (b == ok) {
            if (apply->isEnabled()) {
                save();
            }
            dlg.accept();
        } else {
            dlg.reject();
        }
    });
    dlg.exec();
}

/* ---------------- Personalization ---------------- */

struct Theme {
    QString id;
    QString name;
    QString preview;
    QColor color;     // the window colour of the theme (ColorizationColor of its .theme file)
    QString folder;   // wallpapers in ~/.local/share/wallpapers/Windows7/<folder>
    int accent = 1;   // index of that colour in the aeroglassblur effect's list
    QString sounds;   // freedesktop sound theme of its sound scheme
};

/** Windows 7's Aero themes (Windows\Resources\Themes\*.theme of the Ultimate edition) */
inline QList<Theme> win7Themes()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/wallpapers/Windows7/");
    struct Def {
        const char *id, *name, *folder, *first;
        QRgb argb;
        int accent;
        const char *sounds;
    };
    static const Def defs[] = {
        {"windows7", "Windows 7", "Windows", "img0.jpg", 0x6B74B8FC, 1, "Windows 7"},
        {"architecture", "Architecture", "Architecture", "img13.jpg", 0xA80046AD, 2, "Windows 7 Cityscape"},
        {"characters", "Characters", "Characters", "img19.jpg", 0x6698844C, 13, "Windows 7 Characters"},
        {"landscapes", "Landscapes", "Landscapes", "img7.jpg", 0x80555555, 15, "Windows 7 Landscape"},
        {"nature", "Nature", "Nature", "img1.jpg", 0x528D5A94, 12, "Windows 7 Garden"},
        {"scenes", "Scenes", "Scenes", "img25.jpg", 0x856E3BA1, 11, "Windows 7 Quirky"},
    };
    QList<Theme> list;
    for (const Def &d : defs) {
        Theme t;
        t.id = QLatin1String(d.id);
        t.name = tr7(d.name);
        t.folder = root + QLatin1String(d.folder);
        t.preview = t.folder + QLatin1Char('/') + QLatin1String(d.first);
        t.color = QColor::fromRgba(d.argb);
        t.accent = d.accent;
        t.sounds = QString::fromUtf8(d.sounds);
        list.append(t);
    }
    return list;
}

/** Windows 7's names of the window colours, in the order of the aeroglassblur effect's list */
inline QString windowColorName(int index)
{
    static const QStringList names{tr7("Custom"), tr7("Sky"), tr7("Twilight"), tr7("Sea"), tr7("Leaf"),
                                   tr7("Lime"), tr7("Sun", "color"), tr7("Pumpkin"), tr7("Ruby"), tr7("Fuchsia"),
                                   tr7("Blush"), tr7("Violet"), tr7("Lavender"), tr7("Taupe"),
                                   tr7("Chocolate"), tr7("Slate"), tr7("Frost")};
    return names.value(index, tr7("Custom"));
}

inline KConfigGroup win7ThemeConfig()
{
    static KSharedConfig::Ptr cfg = KSharedConfig::openConfig(QStringLiteral("seven-themerc"));
    cfg->reparseConfiguration();
    return cfg->group(QStringLiteral("Theme"));
}

/** Applies a theme the way Windows does: the desktop background (a slide show changing every 30 minutes),
 *  the window colour and the sound scheme */
inline void applyWin7Theme(const Theme &t)
{
    // background, through Plasma's scripting interface for every desktop
    const QStringList images = t.folder.isEmpty() ? QStringList() : QDir(t.folder).entryList({QStringLiteral("*.jpg")}, QDir::Files, QDir::Name);
    QString script;
    if (images.size() <= 1) {
        script = QStringLiteral("var all = desktops(); for (var i = 0; i < all.length; ++i) { var d = all[i];"
                                " d.wallpaperPlugin = 'org.kde.image'; d.currentConfigGroup = ['Wallpaper', 'org.kde.image', 'General'];"
                                " d.writeConfig('Image', 'file://%1'); d.writeConfig('FillMode', 2); }")
                     .arg(t.preview);
    } else {
        script = QStringLiteral("var all = desktops(); for (var i = 0; i < all.length; ++i) { var d = all[i];"
                                " d.wallpaperPlugin = 'org.kde.slideshow'; d.currentConfigGroup = ['Wallpaper', 'org.kde.slideshow', 'General'];"
                                " d.writeConfig('SlidePaths', '%1'); d.writeConfig('SlideInterval', 1800); d.writeConfig('SlideshowMode', 1);"
                                " d.writeConfig('FillMode', 2); }")
                     .arg(t.folder);
    }
    QDBusInterface shell(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"), QStringLiteral("org.kde.PlasmaShell"));
    shell.call(QStringLiteral("evaluateScript"), script);

    // window colour: the effect keeps the colour as hue/saturation/brightness/intensity
    KConfigGroup blur(KSharedConfig::openConfig(QStringLiteral("kwinrc")), QStringLiteral("Effect-aeroglassblur"));
    blur.writeEntry("AccentColorGroup", 0);
    blur.writeEntry("AccentColorName", t.accent);
    blur.writeEntry("AeroHue", t.color.hslHue() < 0 ? 0 : t.color.hslHue());
    blur.writeEntry("AeroSaturation", int(t.color.hsvSaturationF() * 100.0));
    blur.writeEntry("AeroBrightness", int(t.color.valueF() * 100.0));
    blur.writeEntry("AeroIntensity", t.color.alpha());
    blur.sync();
    QDBusInterface effects(QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QStringLiteral("org.kde.kwin.Effects"));
    effects.call(QStringLiteral("reconfigureEffect"), QStringLiteral("aeroglassblur"));

    // sounds: the scheme and every event back to what the scheme gives it
    KConfigGroup snd(KSharedConfig::openConfig(QStringLiteral("kdeglobals")), QStringLiteral("Sounds"));
    snd.writeEntry("Theme", t.sounds, KConfig::Notify);
    snd.sync();
    for (const Win7Sound::SoundEvent &e : Win7Sound::soundEvents()) {
        KConfig defaults(QStringLiteral("knotifications6/") + e.app + QStringLiteral(".notifyrc"), KConfig::NoGlobals, QStandardPaths::GenericDataLocation);
        Win7Sound::writeEvent(e, KConfigGroup(&defaults, QStringLiteral("Event/") + e.event).readEntry("Sound", QString()));
    }

    KConfigGroup g = win7ThemeConfig();
    g.writeEntry("Current", t.id);
    g.sync();
}

inline QList<Theme> themes()
{
    QList<Theme> list;
    QSet<QString> seen;
    const QStringList roots{QDir::homePath() + QStringLiteral("/.local/share/plasma/look-and-feel"), QStringLiteral("/usr/share/plasma/look-and-feel")};
    for (const QString &root : roots) {
        for (const QString &id : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (seen.contains(id)) {
                continue;
            }
            QFile f(root + QLatin1Char('/') + id + QStringLiteral("/metadata.json"));
            if (!f.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QJsonObject plugin = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("KPlugin")).toObject();
            QString name = plugin.value(QStringLiteral("Name[uk]")).toString();
            if (name.isEmpty()) {
                name = plugin.value(QStringLiteral("Name")).toString(id);
            }
            if (id == QLatin1String("authui7")) {
                name = QStringLiteral("Windows 7");
            }
            seen.insert(id);
            list.append({id, name, root + QLatin1Char('/') + id + QStringLiteral("/contents/previews/preview.png")});
        }
    }
    std::sort(list.begin(), list.end(), [](const Theme &a, const Theme &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return list;
}

inline QString currentTheme()
{
    return KConfig(QStringLiteral("kdeglobals")).group(QStringLiteral("KDE")).readEntry("LookAndFeelPackage", QString());
}

/** Theme in the gallery: picture with a glass square on it, name below */
class ThemeTile : public QWidget
{
public:
    ThemeTile(const Theme &t, bool selected, std::function<void()> action)
        : m_theme(t)
        , m_selected(selected)
        , m_action(std::move(action))
    {
        // Windows 7 (measured): 136x126, picture 92x61 at (34,11), window-colour square 60x60 at (6,38),
        // name centred on y 113; a theme with several backgrounds shows three of them fanned out
        setFixedSize(136, 126);
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
        setToolTip(t.name);
        QStringList pictures;
        if (!t.folder.isEmpty()) {
            const QStringList all = QDir(t.folder).entryList({QStringLiteral("*.jpg"), QStringLiteral("*.png")}, QDir::Files, QDir::Name);
            for (const QString &f : all) {
                pictures << t.folder + QLatin1Char('/') + f;
            }
        }
        pictures.removeAll(t.preview);
        pictures.prepend(t.preview);
        for (int i = 0; i < qMin(3, int(pictures.size())); ++i) {
            const QImage img(pictures.at(i));
            if (!img.isNull()) {
                m_pictures << QPixmap::fromImage(img.scaled(92, 61, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation).copy(0, 0, 92, 61));
            }
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_selected || underMouse()) {
            QLinearGradient g(0, 0, 0, height());
            g.setColorAt(0, m_selected ? QColor(0xec, 0xf4, 0xff) : QColor(0xf5, 0xf9, 0xfe));
            g.setColorAt(1, m_selected ? QColor(0xd0, 0xe6, 0xff) : QColor(0xe9, 0xf2, 0xfd));
            p.setBrush(g);
            p.setPen(m_selected ? QColor(0x84, 0xac, 0xdd) : QColor(0xb8, 0xd6, 0xfb));
            p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
            p.setPen(QColor(255, 255, 255, 170));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 2, 2);
        }
        // the theme's own thumbnail as Windows 7 draws it (cut from Windows 7, ~/.local/share/seven-aero/themecpl)
        static const QString dir = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-aero/themecpl"), QStandardPaths::LocateDirectory);
        const QPixmap genuine(dir + QStringLiteral("/tile-") + m_theme.id + QStringLiteral(".png"));
        if (!genuine.isNull()) {
            p.drawPixmap(0, 0, genuine);
            p.setPen(Qt::black);
            p.drawText(QRect(0, 104, width(), 18), Qt::AlignHCenter | Qt::AlignVCenter, fontMetrics().elidedText(m_theme.name, Qt::ElideRight, width() - 6));
            return;
        }
        const QRect pic(34, 11, 92, 61);
        if (m_pictures.size() > 1) {
            // fanned out like Windows 7: the back ones turned and moved up-right
            for (int i = m_pictures.size() - 1; i >= 0; --i) {
                p.save();
                p.translate(pic.center());
                p.rotate(-6.0 * i);
                p.translate(4 * i, -5 * i);
                const QRect r(-pic.width() / 2, -pic.height() / 2, pic.width(), pic.height());
                p.setPen(QColor(255, 255, 255));
                p.setBrush(Qt::NoBrush);
                p.drawPixmap(r, m_pictures.at(i));
                p.drawRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5));
                p.restore();
            }
        } else if (!m_pictures.isEmpty()) {
            p.drawPixmap(pic, m_pictures.first());
        } else {
            p.fillRect(pic, QColor(0x3b, 0x6e, 0xa5));
        }
        // the window colour square of Windows 7 theme thumbnails, measured on the genuine ones: the colour
        // "un-blended" from white, most opaque at 3/4 height, its strength following the colour's intensity
        QRectF glass(6.5, 38.5, 59, 59);
        const QColor col = m_theme.color.isValid() ? m_theme.color : QColor(0x74, 0xb8, 0xfc, 0x6b);
        const int a0 = qMax(1, 255 - qMin(col.red(), qMin(col.green(), col.blue())));
        auto unwhite = [a0](int c) {
            return qBound(0, int(255 - (255 - c) * 255.0 / a0), 255);
        };
        const QColor tint(unwhite(col.red()), unwhite(col.green()), unwhite(col.blue()));
        const double peak = qBound(40.0, col.alpha() * 0.62, 200.0);
        QLinearGradient gg(0, glass.top(), 0, glass.bottom());
        const QList<QPair<double, double>> profile{{0, 0.30}, {0.2, 0.36}, {0.4, 0.52}, {0.55, 0.66}, {0.72, 1.0}, {0.88, 0.81}, {1, 0.55}};
        for (const auto &[pos, f] : profile) {
            QColor c = tint;
            c.setAlpha(int(peak * f));
            gg.setColorAt(pos, c);
        }
        p.setBrush(gg);
        QColor border = tint.darker(140);
        border.setAlpha(qBound(90, int(peak * 1.3), 220));
        p.setPen(border);
        p.drawRoundedRect(glass, 4, 4);
        // the glass sheen: a light band across the upper-left half
        {
            QPainterPath clip;
            clip.addRoundedRect(glass.adjusted(1, 1, -1, -1), 3, 3);
            p.save();
            p.setClipPath(clip);
            QLinearGradient sheen(glass.topLeft(), glass.bottomRight());
            sheen.setColorAt(0, QColor(255, 255, 255, 120));
            sheen.setColorAt(0.45, QColor(255, 255, 255, 70));
            sheen.setColorAt(0.46, QColor(255, 255, 255, 0));
            sheen.setColorAt(1, QColor(255, 255, 255, 0));
            p.fillRect(glass, sheen);
            p.restore();
        }
        p.setPen(QColor(255, 255, 255, 150));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(glass.adjusted(1, 1, -1, -1), 3, 3);
        p.setPen(Qt::black);
        p.drawText(QRect(0, 104, width(), 18), Qt::AlignHCenter | Qt::AlignVCenter, fontMetrics().elidedText(m_theme.name, Qt::ElideRight, width() - 6));
    }
    void enterEvent(QEnterEvent *) override
    {
        update();
    }
    void leaveEvent(QEvent *) override
    {
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) && m_action) {
            m_action();
        }
    }

private:
    Theme m_theme;
    bool m_selected;
    std::function<void()> m_action;
    QList<QPixmap> m_pictures;
};

/** Wallpaper picture + name of the desktop (first desktop containment with an image wallpaper) */
inline std::pair<QPixmap, QString> currentWallpaper()
{
    KConfig cfg(QStringLiteral("plasma-io.gitgud.wackyideas.desktop-appletsrc"));
    KConfigGroup containments = cfg.group(QStringLiteral("Containments"));
    for (const QString &id : containments.groupList()) {
        KConfigGroup c = containments.group(id);
        // only desktops: panels and the tray carry a wallpaper plugin entry too, without a picture
        if (c.readEntry("activityId", QString()).isEmpty() || c.readEntry("plugin", QString()).contains(QLatin1String("panel"))) {
            continue;
        }
        if (c.readEntry("wallpaperplugin", QString()) == QLatin1String("org.kde.slideshow")) {
            const QStringList dirs = c.group(QStringLiteral("Wallpaper")).group(QStringLiteral("org.kde.slideshow")).group(QStringLiteral("General")).readEntry("SlidePaths", QStringList());
            QString first;
            for (const QString &d : dirs) {
                const QStringList pics = QDir(QString(d).remove(QStringLiteral("file://"))).entryList({QStringLiteral("*.jpg"), QStringLiteral("*.png")}, QDir::Files, QDir::Name);
                if (!pics.isEmpty()) {
                    first = QString(d).remove(QStringLiteral("file://")) + QLatin1Char('/') + pics.first();
                    break;
                }
            }
            QPixmap pm;
            const QImage img(first);
            if (!img.isNull()) {
                pm = QPixmap::fromImage(img.scaled(76, 48, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation).copy(0, 0, 76, 48));
            }
            return {pm, tr7("Slide Show")};
        }
        if (c.readEntry("wallpaperplugin", QString()) != QLatin1String("org.kde.image")) {
            continue;
        }
        QString image = c.group(QStringLiteral("Wallpaper")).group(QStringLiteral("org.kde.image")).group(QStringLiteral("General")).readEntry("Image", QString());
        if (image.isEmpty()) { // Plasma's default wallpaper
            const QString theme = KConfig(QStringLiteral("plasmarc")).group(QStringLiteral("Wallpapers")).readEntry("defaultWallpaperTheme", QStringLiteral("Next"));
            image = QStringLiteral("/usr/share/wallpapers/") + theme;
        }
        image.remove(QStringLiteral("file://"));
        QString name = QFileInfo(image).completeBaseName();
        if (image.endsWith(QLatin1String("/Windows7/Windows/img0.jpg"))) {
            name = tr7("Harmony"); // what Windows 7 calls its default background
        }
        QString picture = image;
        if (QFileInfo(image).isDir()) { // wallpaper package
            QFile meta(image + QStringLiteral("/metadata.json"));
            if (meta.open(QIODevice::ReadOnly)) {
                const QJsonObject plugin = QJsonDocument::fromJson(meta.readAll()).object().value(QStringLiteral("KPlugin")).toObject();
                name = plugin.value(QStringLiteral("Name")).toString(name);
            }
            // the picture made for a 16:9 screen, or the first one
            const QStringList pics = QDir(image + QStringLiteral("/contents/images")).entryList({QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.svg")}, QDir::Files, QDir::Name);
            QString best = pics.value(0);
            for (const QString &f : pics) {
                if (f.startsWith(QLatin1String("1920x1080"))) {
                    best = f;
                }
            }
            picture = best.isEmpty() ? QString() : image + QStringLiteral("/contents/images/") + best;
        }
        QImage img(picture);
        QPixmap pm;
        if (!img.isNull()) {
            pm = QPixmap::fromImage(img.scaled(76, 48, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation).copy(0, 0, 76, 48));
        }
        return {pm, name};
    }
    return {QPixmap(), tr7("Unknown")};
}

/** One of the four items under the gallery */
inline QWidget *bottomItem(const QPixmap &picture, const QString &title, const QString &subtitle, std::function<void()> action)
{
    auto *w = new QWidget;
    // Windows 7: the four items share the width and get narrower with the window
    w->setMinimumWidth(90);
    w->setMaximumWidth(150);
    w->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    auto *pic = new QLabel;
    pic->setPixmap(picture);
    pic->setFixedHeight(54);
    pic->setAlignment(Qt::AlignCenter);
    pic->setCursor(Qt::PointingHandCursor);
    v->addWidget(pic);
    auto *link = new Link(title, action);
    link->setAlignment(Qt::AlignHCenter);
    link->setWordWrap(true);
    v->addWidget(link);
    auto *sub = grey(subtitle);
    sub->setAlignment(Qt::AlignHCenter);
    sub->setWordWrap(true);
    v->addWidget(sub);
    struct Click : public QObject {
        std::function<void()> fn;
        bool eventFilter(QObject *, QEvent *e) override
        {
            if (e->type() == QEvent::MouseButtonRelease) {
                fn();
                return true;
            }
            return false;
        }
    };
    auto *click = new Click;
    click->fn = std::move(action);
    click->setParent(pic);
    pic->installEventFilter(click);
    return w;
}

inline QPixmap colorSwatch()
{
    QPixmap pm(52, 48);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r(2, 1, 48, 45);
    QLinearGradient g(0, r.top(), 0, r.bottom());
    g.setColorAt(0, QColor(0xf4, 0xf8, 0xfd));
    g.setColorAt(0.5, QColor(0xd6, 0xe6, 0xf7));
    g.setColorAt(1, QColor(0xc4, 0xda, 0xf2));
    p.setBrush(g);
    p.setPen(QColor(0x8a, 0xa2, 0xbd));
    p.drawRoundedRect(r, 4, 4);
    p.setPen(QColor(255, 255, 255, 180));
    p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 3, 3);
    return pm;
}

/** The window colour now set (Window Color page), as the glass square of the theme thumbnails shows it */
inline QColor currentGlassColor()
{
    const KConfigGroup g(KSharedConfig::openConfig(QStringLiteral("kwinrc")), QStringLiteral("Effect-aeroglassblur"));
    QColor c = QColor::fromHsv(qBound(0, g.readEntry("AeroHue", 211), 359), qBound(0, int(g.readEntry("AeroSaturation", 54) * 2.55), 255),
                               qBound(0, int(g.readEntry("AeroBrightness", 98) * 2.55), 255));
    c.setAlpha(qBound(0, g.readEntry("AeroIntensity", 107), 255));
    return c;
}

/** "My Themes" saved with "Save theme": ~/.config/seven-themerc [Saved][<name>] */
inline QList<Theme> savedThemes()
{
    QList<Theme> list;
    KConfigGroup saved = win7ThemeConfig().parent().group(QStringLiteral("Saved"));
    for (const QString &name : saved.groupList()) {
        const KConfigGroup g = saved.group(name);
        Theme t;
        t.id = QStringLiteral("saved:") + name;
        t.name = name;
        t.preview = g.readEntry("Image", QString());
        t.color = g.readEntry("Color", QColor());
        t.accent = g.readEntry("Accent", 0);
        t.sounds = g.readEntry("Sounds", QString());
        list.append(t);
    }
    return list;
}

/** Tiles left to right, wrapping onto the next row when the window narrows (Windows 7 theme gallery) */
class TileFlow : public QLayout
{
public:
    explicit TileFlow(QWidget *parent = nullptr)
        : QLayout(parent)
    {
        setContentsMargins(0, 0, 0, 0);
        setSpacing(0);
    }
    ~TileFlow() override
    {
        while (QLayoutItem *i = takeAt(0)) {
            delete i;
        }
    }
    void addItem(QLayoutItem *item) override
    {
        m_items.append(item);
    }
    int count() const override
    {
        return m_items.size();
    }
    QLayoutItem *itemAt(int i) const override
    {
        return m_items.value(i);
    }
    QLayoutItem *takeAt(int i) override
    {
        return (i >= 0 && i < m_items.size()) ? m_items.takeAt(i) : nullptr;
    }
    Qt::Orientations expandingDirections() const override
    {
        return {};
    }
    bool hasHeightForWidth() const override
    {
        return true;
    }
    int heightForWidth(int w) const override
    {
        return place(QRect(0, 0, w, 0), false);
    }
    void setGeometry(const QRect &r) override
    {
        QLayout::setGeometry(r);
        place(r, true);
    }
    QSize sizeHint() const override
    {
        return minimumSize();
    }
    QSize minimumSize() const override
    {
        QSize s;
        for (QLayoutItem *i : m_items) {
            s = s.expandedTo(i->minimumSize());
        }
        return s;
    }

private:
    int place(const QRect &r, bool apply) const
    {
        int x = r.x(), y = r.y(), rowH = 0;
        for (QLayoutItem *i : m_items) {
            const QSize sz = i->sizeHint();
            if (x + sz.width() > r.right() + 1 && x > r.x()) {
                x = r.x();
                y += rowH;
                rowH = 0;
            }
            if (apply) {
                i->setGeometry(QRect(QPoint(x, y), sz));
            }
            x += sz.width();
            rowH = qMax(rowH, sz.height());
        }
        return y + rowH - r.y();
    }
    QList<QLayoutItem *> m_items;
};

inline QWidget *buildPersonalizePage(const Nav &nav)
{
    auto *side = sidePane(nav.home,
                          {{tr7("Change desktop icons"), [] { desktopIconsDialog(QApplication::activeWindow()); }},
                           {tr7("Change mouse pointers"), [] { QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--mouse"), QStringLiteral("1")}); }},
                           {tr7("Change your account picture", "personalization"), [nav] { nav.open(28); /* Win7Users::PictureView, own account */ }}},
                          {{tr7("Display"), [nav] { nav.open(DisplayView); }},
                           {tr7("Taskbar and Start Menu"), [] { QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--taskbar-props")}); }},
                           {tr7("Ease of Access Center"), [] { QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--ease")}); }}});

    auto *main = new QWidget;
    auto *mv = new QVBoxLayout(main);
    mv->setContentsMargins(22, 18, 22, 14);
    mv->setSpacing(0);
    mv->addWidget(pageTitle(tr7("Change the visuals and sounds on your computer")));
    mv->addSpacing(6);
    mv->addWidget(plain(tr7("Click a theme to change the desktop background, window color, sounds, and screen saver all at once.")));
    mv->addSpacing(10);

    // gallery
    auto *galleryArea = new QScrollArea;
    galleryArea->setWidgetResizable(true);
    galleryArea->setMinimumHeight(130); // Windows 7: the gallery gives up height first, the items under it stay
    galleryArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    galleryArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // Windows 7: the tiles wrap instead
    galleryArea->setStyleSheet(QStringLiteral("QScrollArea { border: 1px solid #828790; background: #ffffff; }"));
    auto *gallery = new QWidget;
    gallery->setObjectName(QStringLiteral("win7gallery"));
    gallery->setStyleSheet(QStringLiteral("#win7gallery { background: #ffffff; }"));
    auto *gv = new QVBoxLayout(gallery);
    gv->setContentsMargins(10, 8, 10, 10);
    gv->setSpacing(6);
    QString current = win7ThemeConfig().readEntry("Current", QString());
    const QList<Theme> aero = win7Themes();
    auto addGroup = [gv, &current, nav](const QString &title, const QList<Theme> &list) {
        if (list.isEmpty()) {
            return;
        }
        auto *header = new SectionHeader(QStringLiteral("%1 (%2)").arg(title).arg(list.size()));
        header->setStyleSheet(QStringLiteral("color: #1e3287;"));
        gv->addWidget(header);
        auto *grid = new TileFlow; // Windows 7: the tiles touch and wrap with the window
        for (int i = 0; i < list.size(); ++i) {
            const Theme t = list.at(i);
            grid->addWidget(new ThemeTile(t, t.id == current,
                                          [t, nav] {
                                              QApplication::setOverrideCursor(Qt::WaitCursor);
                                              applyWin7Theme(t);
                                              QApplication::restoreOverrideCursor();
                                              QTimer::singleShot(300, [nav] {
                                                  nav.open(PersonalizeView);
                                              });
                                          }));
        }
        gv->addLayout(grid);
        gv->addSpacing(6);
    };
    {
        // Windows 7: a background or colour changed after picking a theme makes an "Unsaved Theme" in My Themes
        KConfig plasma(QStringLiteral("plasma-io.gitgud.wackyideas.desktop-appletsrc"));
        QString image;
        KConfigGroup cs = plasma.group(QStringLiteral("Containments"));
        for (const QString &id : cs.groupList()) {
            KConfigGroup c = cs.group(id);
            if (!c.readEntry("activityId", QString()).isEmpty() && c.readEntry("wallpaperplugin", QString()) == QLatin1String("org.kde.image")) {
                image = c.group(QStringLiteral("Wallpaper")).group(QStringLiteral("org.kde.image")).group(QStringLiteral("General")).readEntry("Image", QString());
                image.remove(QStringLiteral("file://"));
                break;
            }
        }
        const Theme *cur = nullptr;
        for (const Theme &t : aero) {
            if (t.id == current) {
                cur = &t;
            }
        }
        const QList<Theme> saved = savedThemes();
        for (const Theme &t : saved) {
            if (t.id == current) {
                cur = &t;
            }
        }
        const bool matches = cur && !image.isEmpty()
            && (cur->folder.isEmpty() ? image == cur->preview : image.startsWith(cur->folder + QLatin1Char('/')));
        QList<Theme> mine = saved;
        if (!image.isEmpty() && !matches) {
            // Windows 7: the unsaved theme shows the background and the window colour now set
            Theme unsaved;
            unsaved.id = QStringLiteral("unsaved");
            unsaved.name = tr7("Unsaved Theme");
            unsaved.preview = image;
            unsaved.color = currentGlassColor();
            mine.prepend(unsaved);
            current = unsaved.id; // no Aero theme is the selected one then
        }
        if (!mine.isEmpty()) {
            auto *header = new SectionHeader(tr7("My Themes (%1)").arg(mine.size()));
            header->setStyleSheet(QStringLiteral("color: #1e3287;"));
            gv->addWidget(header);
            auto *grid = new TileFlow;
            for (int i = 0; i < mine.size(); ++i) {
                const Theme t = mine.at(i);
                std::function<void()> act;
                if (t.id != QLatin1String("unsaved")) {
                    act = [t, nav] {
                        applyWin7Theme(t);
                        QTimer::singleShot(300, [nav] {
                            nav.open(PersonalizeView);
                        });
                    };
                }
                grid->addWidget(new ThemeTile(t, t.id == current, act));
            }
            gv->addLayout(grid);
            if (mine.first().id == QLatin1String("unsaved")) {
                // "Save theme" under the gallery row, right-aligned like Windows 7
                auto *links = new QHBoxLayout;
                links->addStretch(1);
                const Theme unsaved = mine.first();
                links->addWidget(new Link(tr7("Save theme"), [unsaved, nav] {
                    bool ok = false;
                    const QString name = QInputDialog::getText(QApplication::activeWindow(), tr7("Save Theme As"), tr7("Theme name:"),
                                                               QLineEdit::Normal, tr7("Unsaved Theme"), &ok)
                                             .trimmed();
                    if (!ok || name.isEmpty()) {
                        return;
                    }
                    KConfigGroup g = win7ThemeConfig().parent().group(QStringLiteral("Saved")).group(name);
                    g.writeEntry("Image", unsaved.preview);
                    g.writeEntry("Color", unsaved.color);
                    g.writeEntry("Accent", KConfigGroup(KSharedConfig::openConfig(QStringLiteral("kwinrc")), QStringLiteral("Effect-aeroglassblur")).readEntry("AccentColorName", 1));
                    g.writeEntry("Sounds", Win7Sound::currentTheme());
                    KConfigGroup c = win7ThemeConfig();
                    c.writeEntry("Current", QStringLiteral("saved:") + name);
                    c.sync();
                    nav.open(PersonalizeView);
                }));
                gv->addLayout(links);
            }
            gv->addSpacing(6);
        }
    }
    addGroup(tr7("Aero Themes"), aero);
    gv->addStretch(1);
    galleryArea->setWidget(gallery);
    mv->addWidget(galleryArea, 1);
    mv->addSpacing(12);

    // background / window colour / sounds / screen saver
    auto *row = new QHBoxLayout;
    row->setSpacing(8);
    row->addStretch(1);
    const auto [wallPicture, wallName] = currentWallpaper();
    row->addWidget(bottomItem(wallPicture, tr7("Desktop Background"), wallName, [nav] { nav.open(83); /* Win7Background::BackgroundView */ }));
    const int accent = KConfig(QStringLiteral("kwinrc")).group(QStringLiteral("Effect-aeroglassblur")).readEntry("AccentColorName", 1);
    row->addWidget(bottomItem(colorSwatch(), tr7("Window Color"), windowColorName(accent), [nav] { nav.open(84); /* Win7WindowColor::ColorView */ }));
    // the scheme name as the "Sounds" tab of the Sound dialog shows it
    const QString soundTheme = Win7Sound::currentTheme();
    QString sounds = tr7("Default");
    for (const Win7Sound::Theme &t : Win7Sound::themes()) {
        if (t.dir == soundTheme) {
            sounds = t.name;
        }
    }
    row->addWidget(bottomItem(QPixmap(QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-aero/themecpl/PNG_86.png"))), tr7("Sounds"), sounds, [nav] {
        auto *dlg = new Win7Sound::SoundDialog(2, QApplication::activeWindow());
        dlg->openSpeech = [] {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--speech")});
        };
        QObject::connect(dlg, &QDialog::finished, [nav] {
            nav.open(PersonalizeView); // refresh the scheme name
        });
        dlg->open();
    }));
    // the screen saver of Win7ScreenSaver (~/.config/seven-screensaverrc), "Відсутня" without one
    const QString saverId = KConfig(QStringLiteral("seven-screensaverrc")).group(QStringLiteral("General")).readEntry("Saver", QStringLiteral("none"));
    QString saverName = tr7("None", "screen saver");
    static const QList<QPair<QString, QString>> saverNames{{QStringLiteral("text3d"), tr7("3D Text")}, {QStringLiteral("blank"), tr7("Blank")},
                                                           {QStringLiteral("bubbles"), tr7("Bubbles")}, {QStringLiteral("mystify"), tr7("Mystify")},
                                                           {QStringLiteral("photos"), tr7("Photos")}, {QStringLiteral("ribbons"), tr7("Ribbons")}};
    for (const auto &n : saverNames) {
        if (n.first == saverId) {
            saverName = n.second;
        }
    }
    const QString saverPic = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                    saverId == QLatin1String("none") ? QStringLiteral("seven-aero/themecpl/PNG_89.png") : QStringLiteral("seven-aero/themecpl/PNG_88.png"));
    row->addWidget(bottomItem(QPixmap(saverPic), tr7("Screen Saver"), saverName, [nav] {
        // its own window and taskbar button with the desk.cpl icon, like Windows 7
        auto *proc = new QProcess;
        QObject::connect(proc, &QProcess::finished, [proc, nav] {
            proc->deleteLater();
            nav.open(PersonalizeView); // refresh the name
        });
        proc->start(QCoreApplication::applicationFilePath(), {QStringLiteral("--screensaver-settings")});
    }));
    row->addStretch(1);
    mv->addLayout(row);
    return pageWithSide(side, main);
}

/* ---------------- kscreen ---------------- */

struct Mode {
    QString id;
    QSize size;
    double refresh = 0;
    QSize scaleFrom; // a lower resolution the GPU stretches to the panel ("scale:WxH" ids)
};

/** lower resolutions offered for a panel that only reports its native mode (Windows 7 lists what the GPU
 *  can scale); applied with xrandr --scale-from by ~/.local/bin/seven-1366, kept in ~/.config/seven-resolution */
inline QList<QSize> scaledSizes(QSize native)
{
    QList<QSize> out;
    for (const QSize &s : {QSize(1768, 992), QSize(1680, 945), QSize(1600, 900), QSize(1440, 810), QSize(1366, 768), QSize(1360, 768),
                           QSize(1280, 720), QSize(1024, 576)}) {
        const double a = double(s.width()) / s.height(), n = double(native.width()) / native.height();
        if (s.width() < native.width() && qAbs(a - n) < 0.02) {
            out << s;
        }
    }
    return out;
}

struct Output {
    QString name;
    bool enabled = false;
    bool primary = false;
    QString modeId;
    int rotation = 1; // 1 none, 2 left, 4 inverted, 8 right
    QPoint pos;
    QSize size;
    QList<Mode> modes;
    int type = 0;
};

inline QList<Output> outputs()
{
    QList<Output> list;
    const QJsonObject doc = QJsonDocument::fromJson(run(QStringLiteral("kscreen-doctor"), {QStringLiteral("-j")}).toUtf8()).object();
    for (const QJsonValue &v : doc.value(QStringLiteral("outputs")).toArray()) {
        const QJsonObject o = v.toObject();
        if (!o.value(QStringLiteral("connected")).toBool()) {
            continue;
        }
        Output out;
        out.name = o.value(QStringLiteral("name")).toString();
        out.enabled = o.value(QStringLiteral("enabled")).toBool();
        out.primary = o.value(QStringLiteral("priority")).toInt() == 1;
        out.modeId = o.value(QStringLiteral("currentModeId")).toVariant().toString();
        out.rotation = o.value(QStringLiteral("rotation")).toInt(1);
        out.pos = QPoint(o.value(QStringLiteral("pos")).toObject().value(QStringLiteral("x")).toInt(), o.value(QStringLiteral("pos")).toObject().value(QStringLiteral("y")).toInt());
        out.size = QSize(o.value(QStringLiteral("size")).toObject().value(QStringLiteral("width")).toInt(), o.value(QStringLiteral("size")).toObject().value(QStringLiteral("height")).toInt());
        out.type = o.value(QStringLiteral("type")).toInt();
        for (const QJsonValue &mv : o.value(QStringLiteral("modes")).toArray()) {
            const QJsonObject m = mv.toObject();
            out.modes.append({m.value(QStringLiteral("id")).toVariant().toString(),
                              QSize(m.value(QStringLiteral("size")).toObject().value(QStringLiteral("width")).toInt(), m.value(QStringLiteral("size")).toObject().value(QStringLiteral("height")).toInt()),
                              m.value(QStringLiteral("refreshRate")).toDouble()});
        }
        list << out;
    }
    std::sort(list.begin(), list.end(), [](const Output &a, const Output &b) {
        return a.primary > b.primary;
    });
    // one screen with a single (native) mode, as the NVIDIA driver reports a laptop panel: add the scaled sizes
    if (list.size() == 1 && !list.first().modes.isEmpty()) {
        Output &o = list.first();
        Mode native = o.modes.first();
        for (const Mode &m : o.modes) {
            if (m.size.width() * m.size.height() > native.size.width() * native.size.height()) {
                native = m;
            }
        }
        QSize sizes(native.size);
        bool onlyNative = true;
        for (const Mode &m : o.modes) {
            onlyNative = onlyNative && m.size == native.size;
        }
        if (onlyNative && !Win7KWin::wayland()) { // (the GPU scaling is done with xrandr: X11 only)
            for (const QSize &sz : scaledSizes(native.size)) {
                o.modes.append({QStringLiteral("scale:%1x%2").arg(sz.width()).arg(sz.height()), sz, native.refresh, sz});
            }
            // the desktop is smaller than the mode: a scaled size is in use
            const QScreen *scr = QGuiApplication::primaryScreen();
            const QSize desk = scr ? scr->geometry().size() : QSize();
            if (desk.isValid() && desk != native.size) {
                for (const Mode &m : o.modes) {
                    if (m.size == desk) {
                        o.modeId = m.id;
                        o.size = desk;
                    }
                }
            }
        }
        Q_UNUSED(sizes)
    }
    return list;
}

/** the native mode id of an output (for kscreen when a scaled size is chosen) */
inline QString nativeModeId(const Output &o)
{
    for (const Mode &m : o.modes) {
        if (!m.scaleFrom.isValid()) {
            return m.id;
        }
    }
    return o.modeId;
}

inline Mode currentMode(const Output &o)
{
    for (const Mode &m : o.modes) {
        if (m.id == o.modeId) {
            return m;
        }
    }
    return {o.modeId, o.size, 0};
}

/** Monitor name from the EDID (the Windows 7 "Generic PnP Monitor" line) */
inline QString monitorName(int index)
{
    QStringList names;
    for (const QString &dir : QDir(QStringLiteral("/sys/class/drm")).entryList({QStringLiteral("card*-*")}, QDir::Dirs | QDir::System)) {
        QFile status(QStringLiteral("/sys/class/drm/%1/status").arg(dir));
        if (!status.open(QIODevice::ReadOnly) || !status.readAll().startsWith("connected")) {
            continue;
        }
        QFile edid(QStringLiteral("/sys/class/drm/%1/edid").arg(dir));
        if (!edid.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray e = edid.readAll();
        QString name;
        for (int off : {54, 72, 90, 108}) {
            if (e.size() >= off + 18 && e.at(off) == 0 && e.at(off + 1) == 0 && static_cast<uchar>(e.at(off + 3)) == 0xfc) {
                name = QString::fromLatin1(e.mid(off + 5, 13)).trimmed();
            }
        }
        names << (name.isEmpty() ? tr7("Generic PnP Monitor") : name);
    }
    return names.value(index, tr7("Generic PnP Monitor"));
}

/** The built-in screen of a laptop (Windows 7 calls it "Mobile PC Display") */
inline bool isLaptopPanel(const QList<Output> &outs, int index)
{
    const QString name = outs.at(index).name;
    if (name.startsWith(QLatin1String("eDP")) || name.startsWith(QLatin1String("LVDS"))) {
        return true;
    }
    // proprietary NVIDIA drivers name the panel DP-n; the kernel still knows it as eDP
    const bool edpConnected = !QDir(QStringLiteral("/sys/class/drm")).entryList({QStringLiteral("card*-eDP-*")}, QDir::Dirs | QDir::System).isEmpty();
    return edpConnected && outs.size() == 1;
}

inline QString rotationName(int r)
{
    switch (r) {
    case 2:
        return QStringLiteral("left");
    case 4:
        return QStringLiteral("inverted");
    case 8:
        return QStringLiteral("right");
    default:
        return QStringLiteral("none");
    }
}

inline bool applyOutputs(QWidget *parent, const QList<Output> &list)
{
    QStringList args;
    for (const Output &o : list) {
        const QString p = QStringLiteral("output.%1.").arg(o.name);
        if (!o.enabled) {
            args << p + QStringLiteral("disable");
            continue;
        }
        const QString mode = o.modeId.startsWith(QLatin1String("scale:")) ? nativeModeId(o) : o.modeId;
        args << p + QStringLiteral("enable") << p + QStringLiteral("mode.") + mode << p + QStringLiteral("rotation.") + rotationName(o.rotation)
             << p + QStringLiteral("position.%1,%2").arg(o.pos.x()).arg(o.pos.y());
        if (o.primary) {
            args << p + QStringLiteral("primary");
        }
    }
    QProcess proc;
    proc.start(QStringLiteral("kscreen-doctor"), args);
    proc.waitForFinished(15000);
    if (proc.exitCode() != 0) {
        QMessageBox::warning(parent, tr7("Screen Resolution"),
                             tr7("The display settings could not be changed.\n\n%1").arg(QString::fromUtf8(proc.readAllStandardError()).trimmed()));
        return false;
    }
    // a single panel: the desktop size is the scaled one (or the native one, which turns scaling off)
    if (list.size() == 1 && list.first().enabled) {
        const Output &o = list.first();
        const QSize want = o.modeId.startsWith(QLatin1String("scale:")) ? currentMode(o).size : QSize();
        QFile f(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/seven-resolution"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QSize sz = want.isValid() ? want : currentMode(o).size;
            f.write(QStringLiteral("%1x%2\n").arg(sz.width()).arg(sz.height()).toUtf8());
        }
        f.close();
        const QString tool = QDir::homePath() + QStringLiteral("/.local/bin/seven-1366");
        if (QFileInfo::exists(tool)) {
            QProcess::execute(tool, want.isValid() ? QStringList{QStringLiteral("%1x%2").arg(want.width()).arg(want.height())} : QStringList{QStringLiteral("off")});
        }
    }
    return true;
}

/** "Do you want to keep these display settings?" with the 15 second countdown */
inline bool confirmKeep(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr7("Display settings"));
    auto *v = new QVBoxLayout(&dlg);
    auto *title = new QLabel(tr7("Do you want to keep these display settings?"));
    QFont f = title->font();
    f.setPixelSize(15);
    title->setFont(f);
    title->setStyleSheet(QStringLiteral("color: #1e3287;"));
    v->addWidget(title);
    int left = 15;
    auto *countdown = plain(QString());
    v->addWidget(countdown);
    auto *buttons = new QDialogButtonBox;
    auto *keep = buttons->addButton(tr7("Save changes"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr7("Revert"), QDialogButtonBox::RejectRole);
    keep->setMinimumWidth(110);
    v->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto update = [&] {
        countdown->setText(tr7("Reverting to previous display settings in %1 seconds.").arg(left));
    };
    update();
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &dlg, [&] {
        if (--left <= 0) {
            dlg.reject();
        }
        update();
    });
    timer.start(1000);
    // a Windows 7 message: no icon, no minimise / maximise (smodrc exception seven-dialog; on Wayland the app id)
    return Win7WindowClass::exec(&dlg, "seven-dialog", "seven-control") == QDialog::Accepted;
}

/** Big numbers on every screen for a moment ("Identify") */
inline void identifyScreens()
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i) {
        auto *w = new QLabel(QString::number(i + 1));
        w->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        w->setAttribute(Qt::WA_TranslucentBackground);
        w->setAttribute(Qt::WA_DeleteOnClose);
        QFont f = w->font();
        f.setPixelSize(160);
        f.setBold(true);
        w->setFont(f);
        w->setStyleSheet(QStringLiteral("color: white; background: rgba(0,0,0,110); border-radius: 8px; padding: 0 30px;"));
        w->adjustSize();
        const QRect g = screens.at(i)->geometry();
        w->move(g.left() + 60, g.bottom() - w->height() - 80);
        w->show();
        QTimer::singleShot(2500, w, &QWidget::close);
    }
}

/** Miniature Windows 7 desktop used by the "Preview" and the monitor pictures:
 *  start menu on the left, an Explorer window with icons, a second window, the taskbar */
inline void paintMiniDesktop(QPainter &p, const QRectF &sc, double k, bool bigWindow)
{
    p.save();
    p.setClipRect(sc);
    QLinearGradient bg(sc.topLeft(), sc.bottomLeft());
    bg.setColorAt(0, QColor(0x3a, 0x4c, 0x9c));
    bg.setColorAt(1, QColor(0x4d, 0x62, 0xb4));
    p.fillRect(sc, bg);
    const double W = sc.width(), H = sc.height();
    auto R = [&](double x, double y, double w, double h) {
        return QRectF(sc.left() + x * W, sc.top() + y * H, w * W, h * H);
    };
    auto lines = [&](QRectF r, int n, QColor c, double gap) {
        p.setPen(QPen(c, qMax(1.0, H * 0.012)));
        for (int i = 0; i < n; ++i) {
            const double y = r.top() + i * gap * H;
            p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        }
    };
    auto window = [&](QRectF r) {
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        g.setColorAt(0, QColor(0xf2, 0xf3, 0xf4));
        g.setColorAt(1, QColor(0xdd, 0xe0, 0xe3));
        p.setPen(QColor(0x8e, 0x96, 0xa0));
        p.setBrush(g);
        p.drawRect(r);
        p.fillRect(QRectF(r.left() + 1, r.top() + 1, r.width() - 1, H * 0.035), QColor(0xe9, 0xec, 0xef));
        // caption buttons
        p.fillRect(QRectF(r.right() - W * 0.07, r.top() + H * 0.008, W * 0.06, H * 0.02), QColor(0xb8, 0xbf, 0xc7));
    };
    // Explorer window with an icon grid
    if (bigWindow) {
        const QRectF ex = R(0.24, 0.13, 0.46 * k, 0.48 * k);
        window(ex);
        lines(QRectF(ex.left() + W * 0.03, ex.top() + H * 0.09, W * 0.07, 0), 5, QColor(0xa9, 0xae, 0xb4), 0.03);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x80, 0x84, 0x88));
        const double cell = W * 0.052 * k;
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                p.drawRect(QRectF(ex.left() + W * 0.14 + c * cell * 1.45, ex.top() + H * 0.09 + r * cell * 1.2, cell, cell * 0.95));
            }
        }
    }
    // second window: a picture pane and text lines
    const QRectF w2 = bigWindow ? R(0.47, 0.34, 0.39 * k, 0.41 * k) : R(0.56, 0.16, 0.36, 0.52);
    window(w2);
    QRectF pane(w2.left() + w2.width() * 0.05, w2.top() + w2.height() * 0.12, w2.width() * 0.58, w2.height() * 0.78);
    QLinearGradient pg(pane.topLeft(), pane.bottomRight());
    pg.setColorAt(0, QColor(0xb8, 0xc2, 0xcc));
    pg.setColorAt(1, QColor(0xa3, 0xae, 0xba));
    p.fillRect(pane, pg);
    lines(QRectF(pane.right() + w2.width() * 0.05, pane.top() + H * 0.01, w2.width() * 0.2, 0), 4, QColor(0xa0, 0xa6, 0xad), 0.025);
    lines(QRectF(pane.right() + w2.width() * 0.05, pane.bottom() - H * 0.08, w2.width() * 0.2, 0), 3, QColor(0xa0, 0xa6, 0xad), 0.025);
    // start menu: white list + dark right column
    const QRectF sm = R(0.06, bigWindow ? 0.43 : 0.36, 0.2 * (bigWindow ? k : 1.0), bigWindow ? 0.48 : 0.52);
    p.setPen(QColor(0x9aa3ad));
    p.setBrush(QColor(0xfa, 0xfb, 0xfc));
    p.drawRect(sm);
    const QRectF col(sm.right(), sm.top() + H * 0.02, W * 0.09 * (bigWindow ? k : 1.0), sm.height() - H * 0.02);
    QLinearGradient cg(col.topLeft(), col.bottomLeft());
    cg.setColorAt(0, QColor(0x3c, 0x45, 0x5e));
    cg.setColorAt(1, QColor(0x25, 0x2c, 0x42));
    p.fillRect(col, cg);
    lines(QRectF(col.left() + col.width() * 0.2, col.top() + H * 0.06, col.width() * 0.6, 0), 6, QColor(0x9d, 0xa6, 0xb8), 0.05);
    for (int i = 0; i < 5; ++i) {
        const double y = sm.top() + H * 0.04 + i * H * 0.06;
        p.fillRect(QRectF(sm.left() + W * 0.012, y - H * 0.012, W * 0.016, H * 0.025), QColor(0x55, 0x5c, 0x66));
        p.setPen(QPen(QColor(0xb0, 0xb5, 0xbb), qMax(1.0, H * 0.012)));
        p.drawLine(QPointF(sm.left() + W * 0.04, y), QPointF(sm.right() - W * 0.02, y));
    }
    // taskbar with the start orb
    const QRectF tb = R(0, 0.93, 1, 0.07);
    QLinearGradient tg(tb.topLeft(), tb.bottomLeft());
    tg.setColorAt(0, QColor(0x5a, 0x6c, 0xb6));
    tg.setColorAt(1, QColor(0x33, 0x44, 0x8c));
    p.fillRect(tb, tg);
    const double orb = tb.height() * 1.1;
    p.drawPixmap(QRectF(tb.left() + W * 0.01, tb.center().y() - orb / 2, orb, orb).toRect(), QIcon::fromTheme(QStringLiteral("start-here")).pixmap(32, 32));
    p.restore();
}

/** Blue monitors with a number in a circle, like the Windows 7 arrangement box */
class MonitorsPreview : public QWidget
{
public:
    MonitorsPreview(const QList<Output> &outs, int *selected)
        : m_outs(outs)
        , m_selected(selected)
    {
        setMinimumHeight(118);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QList<QRect> geo;
        QRect all;
        for (const Output &o : m_outs) {
            if (!o.enabled) {
                continue;
            }
            QRect r(o.pos, (o.rotation == 2 || o.rotation == 8) ? o.size.transposed() : o.size);
            geo << r;
            all = all.united(r);
        }
        if (geo.isEmpty()) {
            return;
        }
        const double s = qMin((width() - 40.0) / all.width(), (height() - 30.0) / all.height());
        const QPointF origin((width() - all.width() * s) / 2 - all.left() * s, (height() - all.height() * s) / 2 - all.top() * s);
        int k = 0;
        for (int i = 0; i < m_outs.size(); ++i) {
            if (!m_outs.at(i).enabled) {
                continue;
            }
            const QRectF r(origin.x() + geo.at(k).left() * s + 2, origin.y() + geo.at(k).top() * s + 2, geo.at(k).width() * s - 4, geo.at(k).height() * s - 4);
            ++k;
            // light blue frame of the selected monitor, like Windows 7
            p.setPen(Qt::NoPen);
            p.setBrush(i == *m_selected ? QColor(0x8e, 0xc5, 0xf5) : QColor(0xb4, 0xbc, 0xc6));
            p.drawRoundedRect(r.adjusted(-3, -3, 3, 3), 2, 2);
            paintMiniDesktop(p, r, 1.0, false);
            p.setPen(QColor(0x2a, 0x3a, 0x70));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);
            const double d = qMin(r.width(), r.height()) * 0.78;
            const QRectF c(r.center().x() - d / 2 - r.width() * 0.04, r.center().y() - d / 2, d, d);
            p.setPen(QPen(QColor(0xe6, 0xec, 0xf6, 235), qMax(3.0, d * 0.07)));
            p.setBrush(QColor(0x2c, 0x33, 0x86, 225));
            p.drawEllipse(c);
            QFont f = font();
            f.setPixelSize(int(d * 0.62));
            f.setBold(true);
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(c, Qt::AlignCenter, QString::number(i + 1));
        }
    }

private:
    QList<Output> m_outs;
    int *m_selected;
};

/* ---------------- adapter / monitor properties ---------------- */

inline QString gpuValue(const QString &field)
{
    return run(QStringLiteral("nvidia-smi"), {QStringLiteral("--query-gpu=") + field, QStringLiteral("--format=csv,noheader,nounits")}).section(QLatin1Char('\n'), 0, 0).trimmed();
}

inline QString modeText(const Mode &m)
{
    return tr7("%1 by %2, True Color (32 bit), %3 Hertz").arg(m.size.width()).arg(m.size.height()).arg(qRound(m.refresh));
}

/** Returns the chosen mode id (refresh rate) or empty */
inline QString advancedDialog(QWidget *parent, const Output &o, int index)
{
    const QString gpu = gpuValue(QStringLiteral("name"));
    const QString vga = run(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("lspci | grep -iE 'vga|3d controller' | head -1 | cut -d: -f3-")}).trimmed();
    const QString adapter = gpu.isEmpty() ? vga : gpu;
    const QString monitor = monitorName(index);
    QDialog dlg(parent);
    dlg.setWindowTitle(tr7("%1 and %2 Properties").arg(monitor, adapter));
    dlg.setMinimumWidth(420);
    auto *v = new QVBoxLayout(&dlg);
    auto *tabs = new QTabWidget;

    // Adapter
    auto *ad = new QWidget;
    auto *av = new QVBoxLayout(ad);
    auto *typeBox = new QGroupBox(tr7("Adapter Type"));
    auto *tl = new QHBoxLayout(typeBox);
    auto *adIcon = new QLabel;
    adIcon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-deskadp")).pixmap(32, 32));
    tl->addWidget(adIcon);
    tl->addWidget(plain(adapter), 1);
    av->addWidget(typeBox);
    auto *infoBox = new QGroupBox(tr7("Adapter Information"));
    auto *grid = new QGridLayout(infoBox);
    int r = 0;
    auto row = [&](const QString &k, const QString &val) {
        if (val.isEmpty()) {
            return;
        }
        grid->addWidget(new QLabel(k), r, 0);
        auto *l = new QLabel(val);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(l, r, 1);
        ++r;
    };
    row(tr7("Chip Type:"), adapter);
    row(tr7("Adapter String:"), vga);
    row(tr7("Driver Version:"), gpuValue(QStringLiteral("driver_version")));
    const QString vram = gpuValue(QStringLiteral("memory.total"));
    row(tr7("Total Available Graphics Memory:"), vram.isEmpty() ? QString() : vram + tr7(" MB"));
    grid->setColumnStretch(1, 1);
    av->addWidget(infoBox);
    auto *modesRow = new QHBoxLayout;
    auto *allModes = new QPushButton(tr7("List All Modes"));
    modesRow->addWidget(allModes);
    modesRow->addStretch(1);
    av->addLayout(modesRow);
    av->addStretch(1);
    tabs->addTab(ad, tr7("Adapter"));

    // Monitor
    auto *mon = new QWidget;
    auto *mv = new QVBoxLayout(mon);
    auto *monBox = new QGroupBox(tr7("Monitor Type"));
    auto *ml = new QHBoxLayout(monBox);
    auto *monIcon = new QLabel;
    monIcon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-deskmon")).pixmap(32, 32));
    ml->addWidget(monIcon);
    ml->addWidget(plain(monitor), 1);
    mv->addWidget(monBox);
    auto *setBox = new QGroupBox(tr7("Monitor Settings"));
    auto *sl = new QVBoxLayout(setBox);
    sl->addWidget(new QLabel(tr7("Screen refresh rate:")));
    auto *refresh = new QComboBox;
    const Mode cur = currentMode(o);
    for (const Mode &m : o.modes) {
        if (m.size == cur.size) {
            refresh->addItem(tr7("%1 Hertz").arg(qRound(m.refresh)), m.id);
        }
    }
    refresh->setCurrentIndex(qMax(0, refresh->findData(o.modeId)));
    sl->addWidget(refresh);
    mv->addWidget(setBox);
    mv->addWidget(new QLabel(tr7("Colors:")));
    auto *depth = new QComboBox;
    depth->addItem(tr7("True Color (32 bit)"));
    depth->setFixedWidth(180);
    mv->addWidget(depth);
    mv->addStretch(1);
    tabs->addTab(mon, tr7("Monitor"));
    v->addWidget(tabs);

    QObject::connect(allModes, &QPushButton::clicked, &dlg, [&dlg, o, refresh] {
        QDialog list(&dlg);
        list.setWindowTitle(tr7("List All Modes"));
        auto *lv = new QVBoxLayout(&list);
        auto *box = new QGroupBox(tr7("List of valid modes"));
        auto *bl = new QVBoxLayout(box);
        auto *w = new QListWidget;
        for (const Mode &m : o.modes) {
            auto *item = new QListWidgetItem(modeText(m), w);
            item->setData(Qt::UserRole, m.id);
            if (m.id == refresh->currentData().toString()) {
                w->setCurrentItem(item);
            }
        }
        bl->addWidget(w);
        lv->addWidget(box);
        auto *bb = new QDialogButtonBox;
        bb->addButton(QStringLiteral("OK"), QDialogButtonBox::AcceptRole);
        bb->addButton(tr7("Cancel"), QDialogButtonBox::RejectRole);
        QObject::connect(bb, &QDialogButtonBox::accepted, &list, &QDialog::accept);
        QObject::connect(bb, &QDialogButtonBox::rejected, &list, &QDialog::reject);
        lv->addWidget(bb);
        list.resize(380, 280);
        if (list.exec() == QDialog::Accepted && w->currentItem()) {
            const int idx = refresh->findData(w->currentItem()->data(Qt::UserRole));
            if (idx >= 0) {
                refresh->setCurrentIndex(idx);
            }
        }
    });
    auto *bb = new QDialogButtonBox;
    bb->addButton(QStringLiteral("OK"), QDialogButtonBox::AcceptRole);
    bb->addButton(tr7("Cancel"), QDialogButtonBox::RejectRole);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted) {
        return {};
    }
    const QString chosen = refresh->currentData().toString();
    return chosen == o.modeId ? QString() : chosen;
}

/* ---------------- Screen Resolution ---------------- */

inline QWidget *buildResolutionPage(const Nav &nav)
{
    const QList<Output> original = outputs();
    auto state = std::make_shared<QList<Output>>(original);
    auto selected = std::make_shared<int>(0);

    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("win7page"));
    page->setAttribute(Qt::WA_StyledBackground, true);
    page->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 18, 0, 20);
    outer->setSpacing(0);
    const int width = 590;
    auto *col = new QWidget;
    col->setFixedWidth(width);
    auto *cv = new QVBoxLayout(col);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    cv->addWidget(pageTitle(tr7("Change the appearance of your displays")));
    cv->addSpacing(12);

    auto *box = new QFrame;
    box->setObjectName(QStringLiteral("win7monbox"));
    box->setStyleSheet(QStringLiteral("#win7monbox { border: 1px solid #a0a0a0; background: #ffffff; }"));
    auto *bh = new QHBoxLayout(box);
    auto *preview = new MonitorsPreview(*state, selected.get());
    bh->addWidget(preview, 1);
    auto *bv = new QVBoxLayout;
    auto *detect = new QPushButton(tr7("Detect"));
    auto *identify = new QPushButton(tr7("Identify"));
    detect->setMinimumWidth(100);
    identify->setMinimumWidth(100);
    bv->addWidget(detect);
    bv->addWidget(identify);
    bv->addStretch(1);
    bh->addLayout(bv);
    cv->addWidget(box);
    cv->addSpacing(16);

    auto *form = new QGridLayout;
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(12);
    auto *screenCombo = new QComboBox;
    auto *resCombo = new QComboBox;
    auto *rotCombo = new QComboBox;
    auto *multiCombo = new QComboBox;
    screenCombo->setMinimumWidth(190);
    resCombo->setMinimumWidth(110);
    rotCombo->setMinimumWidth(190);
    multiCombo->setMinimumWidth(240);
    for (int i = 0; i < state->size(); ++i) {
        screenCombo->addItem(QStringLiteral("%1. %2").arg(i + 1).arg(isLaptopPanel(*state, i) ? tr7("Mobile PC Display") : monitorName(i)));
    }
    rotCombo->addItem(tr7("Landscape"), 1);
    rotCombo->addItem(tr7("Portrait"), 2);
    rotCombo->addItem(tr7("Landscape (flipped)"), 4);
    rotCombo->addItem(tr7("Portrait (flipped)"), 8);
    form->addWidget(new QLabel(tr7("Display:")), 0, 0);
    form->addWidget(screenCombo, 0, 1, Qt::AlignLeft);
    form->addWidget(new QLabel(tr7("Resolution:")), 1, 0);
    form->addWidget(resCombo, 1, 1, Qt::AlignLeft);
    form->addWidget(new QLabel(tr7("Orientation:")), 2, 0);
    form->addWidget(rotCombo, 2, 1, Qt::AlignLeft);
    const bool multi = state->size() > 1;
    if (multi) {
        multiCombo->addItem(tr7("Duplicate these displays"));
        multiCombo->addItem(tr7("Extend these displays"));
        multiCombo->addItem(tr7("Show desktop only on 1"));
        multiCombo->addItem(tr7("Show desktop only on 2"));
        form->addWidget(new QLabel(tr7("Multiple displays:")), 3, 0);
        form->addWidget(multiCombo, 3, 1, Qt::AlignLeft);
        const bool both = state->at(0).enabled && state->at(1).enabled;
        multiCombo->setCurrentIndex(both ? (state->at(0).pos == state->at(1).pos ? 0 : 1) : (state->at(0).enabled ? 2 : 3));
    }
    form->setColumnStretch(2, 1);
    cv->addLayout(form);
    cv->addSpacing(10);
    auto *primaryRow = new QHBoxLayout;
    auto *primaryLabel = new QLabel;
    primaryRow->addWidget(primaryLabel);
    primaryRow->addStretch(1);
    auto *advanced = new Link(tr7("More Options"), nullptr);
    advanced->setWordWrap(false);
    primaryRow->addWidget(advanced);
    cv->addLayout(primaryRow);
    cv->addSpacing(4);
    auto *line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: #d8d8d8;"));
    cv->addWidget(line);
    cv->addSpacing(8);
    auto *projRow = new QHBoxLayout;
    projRow->setSpacing(0);
    auto *proj = new Link(tr7("Connect to a projector"), [] { showDisplaySwitcher(); });
    proj->setWordWrap(false);
    projRow->addWidget(proj);
    projRow->addWidget(new QLabel(tr7(" (or press the ")));
    auto *winKey = new QLabel;
    winKey->setPixmap(QIcon::fromTheme(QStringLiteral("start-here")).pixmap(14, 14));
    projRow->addWidget(winKey);
    projRow->addWidget(new QLabel(tr7(" key and tap P)")));
    projRow->addStretch(1);
    cv->addLayout(projRow);
    cv->addSpacing(8);
    cv->addWidget(new Link(tr7("Make text and other items larger or smaller", "link"), [nav] { nav.open(DisplayView); }));
    outer->addWidget(col, 0, Qt::AlignHCenter);
    outer->addSpacing(18);

    auto *sep = new QFrame;
    sep->setFixedHeight(1);
    sep->setStyleSheet(QStringLiteral("background: #e3e3e3;"));
    outer->addWidget(sep);
    outer->addSpacing(10);
    auto *btnRow = new QWidget;
    btnRow->setFixedWidth(width);
    auto *bl = new QHBoxLayout(btnRow);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->addStretch(1);
    auto *ok = new QPushButton(QStringLiteral("OK"));
    auto *cancel = new QPushButton(tr7("Cancel"));
    auto *apply = new QPushButton(tr7("Apply"));
    for (QPushButton *b : {ok, cancel, apply}) {
        b->setMinimumWidth(78);
        bl->addWidget(b);
    }
    apply->setEnabled(false);
    outer->addWidget(btnRow, 0, Qt::AlignHCenter);
    outer->addStretch(1);

    auto fill = std::make_shared<std::function<void()>>();
    auto updating = std::make_shared<bool>(false);
    *fill = [=] {
        *updating = true;
        const Output &o = state->at(*selected);
        resCombo->clear();
        QList<QSize> sizes;
        for (const Mode &m : o.modes) {
            if (!sizes.contains(m.size)) {
                sizes << m.size;
            }
        }
        std::sort(sizes.begin(), sizes.end(), [](const QSize &a, const QSize &b) {
            return a.width() * a.height() > b.width() * b.height();
        });
        for (const QSize &s : sizes) {
            const bool recommended = &s == &sizes.first();
            resCombo->addItem(QStringLiteral("%1 × %2%3").arg(s.width()).arg(s.height()).arg(recommended ? tr7(" (Recommended)") : QString()), s);
        }
        resCombo->setCurrentIndex(qMax(0, resCombo->findData(currentMode(o).size)));
        rotCombo->setCurrentIndex(qMax(0, rotCombo->findData(o.rotation)));
        primaryLabel->setText(multi && o.primary ? tr7("This is currently your main display.") : QString());
        preview->update();
        *updating = false;
    };
    (*fill)();
    auto changed = [=] {
        apply->setEnabled(true);
        preview->update();
    };
    QObject::connect(screenCombo, &QComboBox::currentIndexChanged, page, [=](int i) {
        *selected = qMax(0, i);
        (*fill)();
    });
    QObject::connect(resCombo, &QComboBox::currentIndexChanged, page, [=] {
        if (*updating) {
            return;
        }
        Output &o = (*state)[*selected];
        const QSize want = resCombo->currentData().toSize();
        // keep the refresh rate close to the current one
        const double rate = currentMode(o).refresh;
        QString best;
        double diff = 1e9;
        for (const Mode &m : o.modes) {
            if (m.size == want && qAbs(m.refresh - rate) < diff) {
                diff = qAbs(m.refresh - rate);
                best = m.id;
            }
        }
        if (!best.isEmpty()) {
            o.modeId = best;
            o.size = want;
        }
        changed();
    });
    QObject::connect(rotCombo, &QComboBox::currentIndexChanged, page, [=] {
        if (*updating) {
            return;
        }
        (*state)[*selected].rotation = rotCombo->currentData().toInt();
        changed();
    });
    QObject::connect(multiCombo, &QComboBox::currentIndexChanged, page, [=](int i) {
        if (!multi) {
            return;
        }
        Output &a = (*state)[0];
        Output &b = (*state)[1];
        a.enabled = i != 3;
        b.enabled = i != 2;
        if (i == 0) {
            b.pos = a.pos;
        } else if (i == 1) {
            b.pos = QPoint(a.pos.x() + a.size.width(), a.pos.y());
        }
        changed();
    });
    QObject::connect(detect, &QPushButton::clicked, page, [nav] {
        nav.open(ResolutionView);
    });
    QObject::connect(identify, &QPushButton::clicked, page, [] {
        identifyScreens();
    });
    auto applyNow = [=]() -> bool {
        if (!apply->isEnabled()) {
            return true;
        }
        if (!applyOutputs(page, *state)) {
            return false;
        }
        if (!confirmKeep(page)) {
            applyOutputs(page, original);
            nav.open(ResolutionView);
            return false;
        }
        apply->setEnabled(false);
        return true;
    };
    QObject::connect(apply, &QPushButton::clicked, page, [=] {
        applyNow();
    });
    QObject::connect(ok, &QPushButton::clicked, page, [=] {
        if (applyNow()) {
            nav.open(DisplayView);
        }
    });
    QObject::connect(cancel, &QPushButton::clicked, page, [nav] {
        nav.back();
    });
    struct Click : public QObject {
        std::function<void()> fn;
        bool eventFilter(QObject *, QEvent *e) override
        {
            if (e->type() == QEvent::MouseButtonRelease) {
                fn();
                return true;
            }
            return false;
        }
    };
    auto *click = new Click;
    click->fn = [=] {
        const QString mode = advancedDialog(page, state->at(*selected), *selected);
        if (!mode.isEmpty()) {
            (*state)[*selected].modeId = mode;
            changed();
        }
    };
    click->setParent(advanced);
    advanced->installEventFilter(click);
    return page;
}

/* ---------------- Display (text size) ---------------- */

inline double currentScale()
{
    return KConfig(QStringLiteral("kdeglobals")).group(QStringLiteral("KScreen")).readEntry("ScaleFactor", 1.0);
}

/** Plasma global scale on X11: applied on the next login */
inline void writeScale(double scale)
{
    KConfig globals(QStringLiteral("kdeglobals"));
    KConfigGroup g = globals.group(QStringLiteral("KScreen"));
    QStringList factors;
    for (const Output &o : outputs()) {
        factors << QStringLiteral("%1=%2").arg(o.name).arg(scale);
    }
    g.writeEntry("ScaleFactor", scale);
    g.writeEntry("ScreenScaleFactors", factors.join(QLatin1Char(';')) + QLatin1Char(';'));
    globals.sync();
    KConfig fonts(QStringLiteral("kcmfonts"));
    fonts.group(QStringLiteral("General")).writeEntry("forceFontDPI", scale == 1.0 ? 0 : qRound(scale * 96));
    fonts.sync();
}

/** Small picture of windows on a blue desktop for the "Preview" box */
inline QPixmap scalePreview(double scale)
{
    QPixmap pm(162, 128);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    // light grey monitor bezel
    QLinearGradient bz(0, 0, 0, 128);
    bz.setColorAt(0, QColor(0xf4, 0xf5, 0xf7));
    bz.setColorAt(1, QColor(0xd4, 0xd9, 0xde));
    p.setPen(QColor(0x9b, 0xa3, 0xac));
    p.setBrush(bz);
    p.drawRoundedRect(QRectF(0.5, 0.5, 161, 127), 3, 3);
    paintMiniDesktop(p, QRectF(8, 8, 146, 110), 1.0 + (scale - 1.0) * 0.9, true);
    p.setPen(QColor(0x7e, 0x88, 0x96));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(8, 8, 146, 110));
    return pm;
}

inline QWidget *buildDisplayPage(const Nav &nav)
{
    auto *side = sidePane(nav.home,
                          {{tr7("Adjust screen resolution"), [nav] { nav.open(ResolutionView); }},
                           {tr7("Adjust brightness"), [nav] { nav.powerPlans(); }},
                           {tr7("Adjust display settings"), [nav] { nav.open(ResolutionView); }},
                           {tr7("Connect to a projector", "task"), [] { showDisplaySwitcher(); }},
                           {tr7("Adjust ClearType text", "task"), [] { QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--cttune")}); }},
                           {tr7("Set custom text size (DPI)"), [] { systemSettings(QStringLiteral("kcm_fonts")); }}},
                          {{tr7("Personalization"), [nav] { nav.open(PersonalizeView); }},
                           {tr7("Devices and Printers"), [] { QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--devices")}); }}});

    auto *main = new QWidget;
    auto *mv = new QVBoxLayout(main);
    mv->setContentsMargins(24, 18, 24, 20);
    mv->setSpacing(0);
    auto *content = new QWidget;
    content->setMaximumWidth(610);
    auto *cv = new QVBoxLayout(content);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    cv->addWidget(pageTitle(tr7("Make it easier to read what's on your screen")));
    cv->addSpacing(6);
    auto *intro = new QLabel(tr7("You can change the size of text and other items on your screen by choosing one of these options. To temporarily enlarge just part of the screen, use the <a href=\"zoom\" style=\"color:#0066cc;\">Magnifier</a> tool."));
    intro->setWordWrap(true);
    QObject::connect(intro, &QLabel::linkActivated, intro, [] {
        QProcess::startDetached(QStringLiteral("qdbus6"), {QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/component/kwin"),
                                                           QStringLiteral("org.kde.kglobalaccel.Component.invokeShortcut"), QStringLiteral("view_zoom_in")});
    });
    cv->addWidget(intro);
    cv->addSpacing(18);

    auto *row = new QHBoxLayout;
    auto *radios = new QVBoxLayout;
    radios->setContentsMargins(20, 0, 0, 0);
    radios->setSpacing(18);
    const double cur = currentScale();
    auto *group = new QButtonGroup(main);
    const QList<std::tuple<double, QString, int>> options{
        {1.0, tr7("Smaller - 100% (default)"), 12},
        {1.25, tr7("Medium - 125%"), 15},
        {1.5, tr7("Larger - 150%"), 18},
    };
    int id = 0;
    for (const auto &[scale, text, px] : options) {
        auto *r = new QRadioButton(text);
        QFont f = r->font();
        f.setPixelSize(px);
        r->setFont(f);
        r->setChecked(qFuzzyCompare(scale, cur));
        group->addButton(r, id++);
        radios->addWidget(r);
    }
    radios->addStretch(1);
    row->addLayout(radios, 1);
    auto *pv = new QVBoxLayout;
    pv->addWidget(new SectionHeader(tr7("Preview")));
    auto *previewPic = new QLabel;
    previewPic->setPixmap(scalePreview(cur));
    previewPic->setAlignment(Qt::AlignCenter);
    pv->addWidget(previewPic);
    pv->addStretch(1);
    auto *pw = new QWidget;
    pw->setFixedWidth(250);
    pw->setLayout(pv);
    row->addWidget(pw);
    cv->addLayout(row);
    cv->addSpacing(14);
    auto *line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: #d8d8d8;"));
    cv->addWidget(line);
    cv->addSpacing(10);
    auto *bottom = new QHBoxLayout;
    auto *warnIcon = new QLabel;
    warnIcon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-net-warn")).pixmap(16, 16));
    auto *warn = plain(tr7("Some items may not fit on your screen if you choose this setting while your display is set to this resolution."));
    bottom->addWidget(warnIcon, 0, Qt::AlignTop);
    bottom->addWidget(warn, 1);
    bottom->addStretch(0);
    auto *apply = new QPushButton(tr7("Apply"));
    apply->setMinimumWidth(96);
    apply->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    apply->setEnabled(false);
    bottom->addWidget(apply, 0, Qt::AlignTop);
    cv->addLayout(bottom);
    mv->addWidget(content);
    mv->addStretch(1);

    int screenHeight = 0;
    for (const Output &o : outputs()) {
        if (o.enabled) {
            screenHeight = qMax(screenHeight, o.size.height());
        }
    }
    auto sync = [=](int chosen) {
        const double scale = std::get<0>(options.at(chosen));
        previewPic->setPixmap(scalePreview(scale));
        apply->setEnabled(!qFuzzyCompare(scale, cur));
        const bool tooBig = screenHeight > 0 && screenHeight / scale < 760;
        warn->setVisible(tooBig && scale > 1.0);
        warnIcon->setVisible(tooBig && scale > 1.0);
    };
    QObject::connect(group, &QButtonGroup::idClicked, main, sync);
    sync(group->checkedId() < 0 ? 0 : group->checkedId());
    QObject::connect(apply, &QPushButton::clicked, main, [=] {
        const int chosen = group->checkedId();
        if (chosen < 0) {
            return;
        }
        writeScale(std::get<0>(options.at(chosen)));
        apply->setEnabled(false);
        QMessageBox box(QMessageBox::Warning, tr7("Display"), tr7("You must log off your computer to apply these changes"), QMessageBox::NoButton, main);
        box.setInformativeText(tr7("Save any open files and close all programs before you log off."));
        QPushButton *now = box.addButton(tr7("Log off now"), QMessageBox::AcceptRole);
        box.addButton(tr7("Log off later"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == now) {
            QProcess::startDetached(QStringLiteral("qdbus6"), {QStringLiteral("org.kde.Shutdown"), QStringLiteral("/Shutdown"), QStringLiteral("org.kde.Shutdown.logout")});
        }
    });
    return pageWithSide(side, main);
}
}
