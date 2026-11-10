#pragma once
#include "win7i18n.h"
/*
 * Tools > Options... (WMP 12 "Параметри") and the library locations dialog ("Розташування бібліотеки ...").
 */
#include "library.h"
#include "res.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>

namespace Opt
{
inline bool value(const QString &key, bool def)
{
    return QSettings().value(QStringLiteral("options/") + key, def).toBool();
}
}

/** Manage libraries > Music / Videos / Pictures: the folders the library watches */
class LocationsDialog : public QDialog
{
public:
    LocationsDialog(MediaItem::Kind kind, QWidget *parent)
        : QDialog(parent)
        , m_kind(kind)
    {
        const QString name = kind == MediaItem::Audio ? tr7("Music") : kind == MediaItem::Video ? tr7("Videos") : tr7("Pictures");
        setWindowTitle(tr7("%1 Library Locations").arg(name));
        auto *l = new QVBoxLayout(this);
        auto *head = new QLabel(tr7("<b>Change how this library gathers its contents: \"%1\"</b><br>When you include a folder in the library, its files appear in Windows Media Player.")
                                    .arg(name),
                                this);
        head->setWordWrap(true);
        l->addWidget(head);
        auto *row = new QHBoxLayout;
        auto *lbl = new QLabel(tr7("Library locations"), this);
        row->addWidget(lbl);
        row->addStretch(1);
        auto *add = new QPushButton(tr7("Add..."), this);
        auto *remove = new QPushButton(tr7("Remove"), this);
        row->addWidget(add);
        row->addWidget(remove);
        l->addLayout(row);
        m_list = new QListWidget(this);
        m_list->setIconSize(QSize(32, 32));
        for (const QString &f : Library::folders(kind)) {
            addFolder(f);
        }
        l->addWidget(m_list, 1);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("OK"));
        bb->button(QDialogButtonBox::Cancel)->setText(tr7("Cancel"));
        l->addWidget(bb);
        connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(add, &QPushButton::clicked, this, [this] {
            const QString d = QFileDialog::getExistingDirectory(this, tr7("Include Folder in Library"), QDir::homePath());
            if (!d.isEmpty()) {
                addFolder(d);
            }
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            delete m_list->currentItem();
        });
        resize(520, 380);
    }
    void accept() override
    {
        QStringList folders;
        for (int i = 0; i < m_list->count(); ++i) {
            folders << m_list->item(i)->data(Qt::UserRole).toString();
        }
        Library::setFolders(m_kind, folders);
        QDialog::accept();
    }

private:
    void addFolder(const QString &f)
    {
        auto *it = new QListWidgetItem(QIcon::fromTheme(QStringLiteral("folder")), QFileInfo(f).fileName() + QStringLiteral("\n") + f, m_list);
        it->setData(Qt::UserRole, f);
    }
    MediaItem::Kind m_kind;
    QListWidget *m_list;
};

class OptionsDialog : public QDialog
{
public:
    explicit OptionsDialog(QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(tr7("Options"));
        auto *l = new QVBoxLayout(this);
        auto *tabs = new QTabWidget(this);
        l->addWidget(tabs);
        tabs->addTab(playerTab(), tr7("Player"));
        tabs->addTab(simpleTab(tr7("Specify where music is stored and change rip settings."),
                               {tr7("Rip music to this location: ") + QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
                                tr7("Format: MP3"), tr7("Audio quality: 192 Kbps")},
                               true),
                     tr7("Rip Music"));
        tabs->addTab(simpleTab(tr7("Specify settings for devices and speakers."), {tr7("Devices: speakers, display")}, true), tr7("Devices"));
        tabs->addTab(simpleTab(tr7("These settings determine how audio CDs and data discs are burned."),
                               {tr7("Burn speed: fastest"), tr7("Apply volume leveling across tracks")}, true),
                     tr7("Burn", "options"));
        tabs->addTab(performanceTab(), tr7("Performance"));
        tabs->addTab(libraryTab(), tr7("Library"));
        tabs->addTab(simpleTab(tr7("Plug-ins extend the capabilities of Windows Media Player."), {tr7("Category: Visualization"), tr7("Album Art, Bars and Waves")}, true),
                     tr7("Plug-ins"));
        tabs->addTab(privacyTab(), tr7("Privacy"));
        tabs->addTab(simpleTab(tr7("Choose whether content can use scripts and whether ActiveX controls are allowed to run."),
                               {tr7("Run script commands when present"), tr7("Play enhanced content that uses Web pages without prompting")}, false),
                     tr7("Security"));
        tabs->addTab(simpleTab(tr7("Configure protocol settings for streaming media."), {tr7("Protocols: RTSP/UDP, RTSP/TCP, HTTP"), tr7("Proxy: use browser settings")}, true),
                     tr7("Network"));

        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply | QDialogButtonBox::Help, this);
        bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("OK"));
        bb->button(QDialogButtonBox::Cancel)->setText(tr7("Cancel"));
        bb->button(QDialogButtonBox::Apply)->setText(tr7("Apply"));
        bb->button(QDialogButtonBox::Help)->setText(tr7("Help"));
        l->addWidget(bb);
        connect(bb, &QDialogButtonBox::accepted, this, [this] {
            apply();
            accept();
        });
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &OptionsDialog::apply);
        setFixedSize(520, 560);
    }

    std::function<void(MediaItem::Kind)> manageLibrary;

private:
    QCheckBox *check(QWidget *parent, QLayout *l, const QString &text, const QString &key, bool def)
    {
        auto *c = new QCheckBox(text, parent);
        c->setChecked(Opt::value(key, def));
        l->addWidget(c);
        m_checks.insert(key, c);
        return c;
    }
    QWidget *header(QWidget *parent, int iconGroup, const QString &text)
    {
        auto *w = new QWidget(parent);
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 6);
        auto *ic = new QLabel(w);
        ic->setPixmap(Res::icon(iconGroup).pixmap(32, 32));
        h->addWidget(ic, 0, Qt::AlignTop);
        auto *t = new QLabel(text, w);
        t->setWordWrap(true);
        h->addWidget(t, 1);
        return w;
    }
    QWidget *playerTab()
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->addWidget(header(w, 102, tr7("Customize updates and other Player settings.")));
        auto *upd = new QGroupBox(tr7("Automatic updates"), w);
        auto *ul = new QVBoxLayout(upd);
        ul->addWidget(new QLabel(tr7("Check for updates:"), upd));
        auto *grp = new QButtonGroup(w);
        const int freq = QSettings().value(QStringLiteral("options/updates"), 1).toInt();
        const QStringList names{tr7("Once a day"), tr7("Once a week"), tr7("Once a month")};
        for (int i = 0; i < 3; ++i) {
            auto *r = new QRadioButton(names[i], upd);
            r->setChecked(i == freq);
            grp->addButton(r, i);
            ul->addWidget(r);
        }
        m_updates = grp;
        l->addWidget(upd);
        auto *set = new QGroupBox(tr7("Player settings"), w);
        auto *sl = new QVBoxLayout(set);
        check(set, sl, tr7("Keep Now Playing on top of other windows"), QStringLiteral("onTop"), false);
        check(set, sl, tr7("Allow screen saver during playback"), QStringLiteral("screensaver"), false);
        check(set, sl, tr7("Add local media files to library when played"), QStringLiteral("addPlayed"), true);
        check(set, sl, tr7("Add remote media files to library when played"), QStringLiteral("addRemote"), false);
        check(set, sl, tr7("Connect to the Internet (overrides other commands)"), QStringLiteral("internet"), false);
        check(set, sl, tr7("Stop playback when switching to a different user"), QStringLiteral("stopOnSwitch"), true);
        check(set, sl, tr7("Allow autohide of playback controls"), QStringLiteral("autoHide"), true);
        check(set, sl, tr7("Save ratings in media files"), QStringLiteral("ratingsInFiles"), false);
        l->addWidget(set);
        l->addStretch(1);
        return w;
    }
    QWidget *performanceTab()
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->addWidget(header(w, 466, tr7("These settings allow you to adjust the Player's performance.")));
        auto *buf = new QGroupBox(tr7("Network buffering"), w);
        auto *bl = new QVBoxLayout(buf);
        auto *r1 = new QRadioButton(tr7("Use default buffering (recommended)"), buf);
        r1->setChecked(true);
        bl->addWidget(r1);
        bl->addWidget(new QRadioButton(tr7("Buffer 5 seconds of content"), buf));
        l->addWidget(buf);
        auto *v = new QGroupBox(tr7("Video acceleration"), w);
        auto *vl = new QVBoxLayout(v);
        auto *slider = new QSlider(Qt::Horizontal, v);
        slider->setRange(0, 2);
        slider->setValue(2);
        slider->setTickPosition(QSlider::TicksBelow);
        auto *sr = new QHBoxLayout;
        sr->addWidget(new QLabel(tr7("None"), v));
        sr->addWidget(slider, 1);
        sr->addWidget(new QLabel(tr7("Full"), v));
        vl->addLayout(sr);
        l->addWidget(v);
        auto *g = new QGroupBox(tr7("Video visibility"), w);
        auto *gl = new QVBoxLayout(g);
        check(g, gl, tr7("Turn on DVD and video captions when available"), QStringLiteral("subtitles"), false);
        check(g, gl, tr7("Fit video to Player if video is larger"), QStringLiteral("fitVideo"), true);
        l->addWidget(g);
        l->addStretch(1);
        return w;
    }
    QWidget *libraryTab()
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->addWidget(header(w, 29513, tr7("Specify settings for the media library.")));
        auto *row = new QHBoxLayout;
        auto *music = new QPushButton(tr7("Manage Music Library..."), w);
        auto *video = new QPushButton(tr7("Manage Video Library..."), w);
        row->addWidget(music);
        row->addWidget(video);
        l->addLayout(row);
        connect(music, &QPushButton::clicked, this, [this] {
            if (manageLibrary) {
                manageLibrary(MediaItem::Audio);
            }
        });
        connect(video, &QPushButton::clicked, this, [this] {
            if (manageLibrary) {
                manageLibrary(MediaItem::Video);
            }
        });
        auto *g = new QGroupBox(tr7("Library settings"), w);
        auto *gl = new QVBoxLayout(g);
        check(g, gl, tr7("Delete files from computer when deleted from library"), QStringLiteral("deleteFiles"), false);
        check(g, gl, tr7("Automatically retrieve album art"), QStringLiteral("findArt"), true);
        check(g, gl, tr7("Retrieve additional information from the Internet"), QStringLiteral("internetInfo"), false);
        check(g, gl, tr7("Save ratings in media files"), QStringLiteral("ratingsInFiles2"), false);
        l->addWidget(g);
        l->addStretch(1);
        return w;
    }
    QWidget *privacyTab()
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->addWidget(header(w, 1151, tr7("Specify privacy settings.")));
        auto *g = new QGroupBox(tr7("Enhanced Playback and Device Experience"), w);
        auto *gl = new QVBoxLayout(g);
        check(g, gl, tr7("Display media information from the Internet"), QStringLiteral("mediaInfo"), false);
        check(g, gl, tr7("Update music files by retrieving media info from the Internet"), QStringLiteral("updateMusic"), false);
        l->addWidget(g);
        auto *h = new QGroupBox(tr7("History"), w);
        auto *hl = new QVBoxLayout(h);
        check(h, hl, tr7("Store and display a list of recently opened files"), QStringLiteral("history"), true);
        auto *clear = new QPushButton(tr7("Clear History"), h);
        hl->addWidget(clear, 0, Qt::AlignLeft);
        connect(clear, &QPushButton::clicked, this, [] {
            QSettings().remove(QStringLiteral("recent"));
        });
        l->addWidget(h);
        l->addStretch(1);
        return w;
    }
    QWidget *simpleTab(const QString &head, const QStringList &lines, bool disabled)
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->addWidget(header(w, 102, head));
        auto *g = new QGroupBox(w);
        auto *gl = new QVBoxLayout(g);
        for (const QString &t : lines) {
            auto *c = new QCheckBox(t, g);
            c->setChecked(true);
            c->setEnabled(!disabled);
            gl->addWidget(c);
        }
        l->addWidget(g);
        auto *note = new QLabel(tr7("<i>These settings are not available on this computer.</i>"), w);
        note->setVisible(disabled);
        l->addWidget(note);
        l->addStretch(1);
        return w;
    }
    void apply()
    {
        QSettings s;
        for (auto it = m_checks.cbegin(); it != m_checks.cend(); ++it) {
            s.setValue(QStringLiteral("options/") + it.key(), it.value()->isChecked());
        }
        s.setValue(QStringLiteral("options/updates"), m_updates->checkedId());
        if (onApply) {
            onApply();
        }
    }

public:
    std::function<void()> onApply;

private:
    QHash<QString, QCheckBox *> m_checks;
    QButtonGroup *m_updates = nullptr;
};
