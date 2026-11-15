#include "win7i18n.h"
/*
 * Windows DVD Maker (Windows 7) for Linux.
 *
 * The Aero wizard of the original: "Add pictures and video to the DVD" (Order / Name / Duration / Status, the
 * disc's minutes, DVD title, Options), "Ready to burn DVD" (the menu preview and the menu styles of
 * OmdProject.dll), Menu text, Customize menu, Slide show and Preview, the DVD-Video options and the burning
 * window. The disc is a real DVD-Video (burner.h); the menus are the styles' still menus (menurender.h).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "burner.h"

#include <AeroQt/wizard.h>

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMimeDatabase>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWizard>
#include <QWizardPage>
#include <QtConcurrent>

namespace
{
const char *const kApp = "seven-dvdmaker";

QPixmap res(const QString &name)
{
    return QPixmap(Dvd::sharedDir() + QLatin1Char('/') + name + QStringLiteral(".png"));
}

/** a 16 px icon of the toolbar strip (DVDMaker.exe bitmap 2271): 1 add, 2 remove, 3 up, 4 down, 5 preview,
 *  6 menu text, 7 customize, 8 scenes, 9 slide show, 10 rename, 11 merge, 12 save style */
QIcon stripIcon(int cell)
{
    const QPixmap strip = res(QStringLiteral("toolbar"));
    return strip.isNull() ? QIcon() : QIcon(strip.copy(cell * 16, 0, 16, 16));
}

QString duration(double s)
{
    const int t = int(s + 0.5);
    return QStringLiteral("%1:%2:%3").arg(t / 3600).arg((t / 60) % 60, 2, 10, QLatin1Char('0')).arg(t % 60, 2, 10, QLatin1Char('0'));
}

const QStringList kVideo{QStringLiteral("avi"), QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("m1v"), QStringLiteral("mp2"), QStringLiteral("mpe"),
                         QStringLiteral("mpv2"), QStringLiteral("wm"), QStringLiteral("wmv"), QStringLiteral("asf"), QStringLiteral("mp4"), QStringLiteral("mkv"),
                         QStringLiteral("mov"), QStringLiteral("webm"), QStringLiteral("m4v"), QStringLiteral("ts"), QStringLiteral("dvr-ms"), QStringLiteral("wtv"),
                         QStringLiteral("3gp"), QStringLiteral("flv")};
const QStringList kPicture{QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("tif"),
                           QStringLiteral("tiff"), QStringLiteral("webp"), QStringLiteral("jfif")};
const QStringList kAudio{QStringLiteral("mp3"), QStringLiteral("wma"), QStringLiteral("wav"), QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("m4a"),
                         QStringLiteral("aac"), QStringLiteral("aif"), QStringLiteral("aiff"), QStringLiteral("opus"), QStringLiteral("au"), QStringLiteral("snd")};

/** the DVD burners: /dev/srN that can write DVD-R (from /proc/sys/dev/cdrom/info) */
QStringList burners()
{
    QFile f(QStringLiteral("/proc/sys/dev/cdrom/info"));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QStringList names, canWrite;
    for (const QByteArray &l : f.readAll().split('\n')) {
        const QList<QByteArray> parts = l.split('\t');
        const QList<QByteArray> vals = l.simplified().split(' ');
        if (l.startsWith("drive name:")) {
            for (int i = 2; i < vals.size(); ++i) {
                names << QString::fromLatin1(vals[i]);
            }
        } else if (l.startsWith("Can write DVD-R:")) {
            for (int i = 3; i < vals.size(); ++i) {
                canWrite << QString::fromLatin1(vals[i]);
            }
        }
    }
    QStringList out;
    for (int i = 0; i < names.size(); ++i) {
        if (canWrite.value(i) == QLatin1String("1")) {
            out << QStringLiteral("/dev/") + names[i];
        }
    }
    return out;
}

/** the disc in the burner: empty, blank, rewritable with data, or written */
enum class Disc { None, Blank, Rewritable, Written, NotDvd };
Disc discState(const QString &dev)
{
    QProcess p;
    p.start(QStringLiteral("dvd+rw-mediainfo"), {dev});
    p.waitForFinished(20000);
    const QString out = QString::fromLocal8Bit(p.readAllStandardOutput() + p.readAllStandardError());
    if (out.contains(QLatin1String("no media")) || out.contains(QLatin1String("not ready")) || out.contains(QLatin1String("non-DVD"))
        || (p.exitCode() != 0 && !out.contains(QLatin1String("Mounted Media")))) {
        return out.contains(QLatin1String("non-DVD")) ? Disc::NotDvd : Disc::None;
    }
    const bool rw = out.contains(QLatin1String("RW")) || out.contains(QLatin1String("RAM"));
    if (out.contains(QLatin1String("Disc status:           blank")) || out.contains(QRegularExpression(QStringLiteral("Disc status:\\s+blank")))) {
        return Disc::Blank;
    }
    return rw ? Disc::Rewritable : Disc::Written;
}
}

using namespace Dvd;

/** the content of the DVD: the list on the first page */
class ItemList : public QTreeWidget
{
    Q_OBJECT
public:
    QList<Item> items;
    explicit ItemList(QWidget *parent)
        : QTreeWidget(parent)
    {
        setColumnCount(4);
        setHeaderLabels({tr7("Order"), tr7("Name"), tr7("Duration"), tr7("Status")});
        setRootIsDecorated(false);
        setIconSize(QSize(64, 48));
        setSelectionMode(QAbstractItemView::ExtendedSelection);
        setAcceptDrops(true);
        viewport()->setAcceptDrops(true);
        setDragDropMode(QAbstractItemView::DropOnly);
        header()->setSectionResizeMode(1, QHeaderView::Stretch);
        header()->resizeSection(0, 70);
        header()->resizeSection(2, 90);
        header()->resizeSection(3, 90);
        header()->setStretchLastSection(false);
    }
    void addFiles(const QStringList &files)
    {
        for (const QString &f : files) {
            const QString ext = QFileInfo(f).suffix().toLower();
            Item it;
            it.path = f;
            it.picture = kPicture.contains(ext);
            if (!it.picture && !kVideo.contains(ext)) {
                continue;
            }
            if (!it.picture) {
                it.duration = probeDuration(f);
                it.thumb = videoFrame(f, qMin(10.0, it.duration / 3)).scaled(128, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            items.append(it);
        }
        refresh();
        Q_EMIT changed();
    }
    double totalMinutes(int pictureSeconds) const
    {
        double s = 0;
        int pics = 0;
        for (const Item &i : items) {
            if (i.picture) {
                ++pics;
            } else {
                s += i.duration;
            }
        }
        return (s + pics * pictureSeconds) / 60.0;
    }
    int pictureCount() const
    {
        return int(std::count_if(items.cbegin(), items.cend(), [](const Item &i) { return i.picture; }));
    }
    void refresh(int pictureSeconds = 7)
    {
        clear();
        // the pictures are one row: "Slide show (N pictures)", where the first picture is
        int order = 1;
        bool slides = false;
        for (int i = 0; i < items.size(); ++i) {
            const Item &it = items[i];
            if (it.picture && slides) {
                continue;
            }
            auto *row = new QTreeWidgetItem(this);
            row->setText(0, QString::number(order++));
            row->setData(0, Qt::UserRole, i);
            if (it.picture) {
                slides = true;
                const int n = pictureCount();
                row->setText(1, n == 1 ? tr7("Slide show (1 picture)") : tr7("Slide show (%1 pictures)").arg(n));
                row->setIcon(1, QIcon(framed(res(QStringLiteral("slideshow")).toImage(), true)));
                row->setText(2, duration(n * pictureSeconds));
            } else {
                row->setText(1, QFileInfo(it.path).completeBaseName());
                row->setIcon(1, QIcon(framed(it.thumb, false)));
                row->setText(2, duration(it.duration));
            }
            row->setText(3, tr7("Ready"));
            row->setSizeHint(0, QSize(0, 56));
        }
    }
    /** a video's frame in the film strip frame (DVDMaker.exe bitmap 450) */
    static QPixmap framed(const QImage &im, bool folder)
    {
        QPixmap out(64, 48);
        out.fill(Qt::transparent);
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        if (folder) {
            p.drawImage(QRect(8, 0, 48, 48), im);
            return out;
        }
        const QPixmap frame = res(QStringLiteral("filmframe"));
        if (!frame.isNull()) {
            p.drawPixmap(out.rect(), frame);
        }
        drawFill(p, QRectF(9, 4, 46, 40), im);
        return out;
    }
    /** the indexes into items of the selected rows (a slide show row: all pictures) */
    QList<int> selectedIndexes2() const
    {
        QList<int> out;
        for (QTreeWidgetItem *r : selectedItems()) {
            const int i = r->data(0, Qt::UserRole).toInt();
            if (items.value(i).picture) {
                for (int k = 0; k < items.size(); ++k) {
                    if (items[k].picture) {
                        out << k;
                    }
                }
            } else {
                out << i;
            }
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

Q_SIGNALS:
    void changed();

protected:
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (e->mimeData()->hasUrls()) {
            e->acceptProposedAction();
        }
    }
    void dragMoveEvent(QDragMoveEvent *e) override
    {
        if (e->mimeData()->hasUrls()) {
            e->acceptProposedAction();
        }
    }
    void dropEvent(QDropEvent *e) override
    {
        QStringList files;
        for (const QUrl &u : e->mimeData()->urls()) {
            if (u.isLocalFile()) {
                const QFileInfo fi(u.toLocalFile());
                if (fi.isDir()) {
                    for (const QFileInfo &c : QDir(fi.filePath()).entryInfoList(QDir::Files, QDir::Name)) {
                        files << c.filePath();
                    }
                } else {
                    files << fi.filePath();
                }
            }
        }
        addFiles(files);
        e->acceptProposedAction();
    }
};

/** a toolbar of the wizard's pages (DVD Maker's light band with "File" and the commands) */
class Band : public QToolBar
{
public:
    explicit Band(QWidget *parent)
        : QToolBar(parent)
    {
        setIconSize(QSize(16, 16));
        setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        setMovable(false);
        setStyleSheet(QStringLiteral("QToolBar { border: 1px solid #c5d1e0; border-radius: 2px; padding: 1px;"
                                     " background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #fbfcfe, stop:0.5 #eef3fa, stop:0.51 #e4ebf5, stop:1 #e8eef7); }"
                                     " QToolButton { padding: 2px 6px; }"));
    }
    QToolButton *button(const QString &text, const QIcon &icon, std::function<void()> fn)
    {
        auto *a = addAction(icon, text);
        QObject::connect(a, &QAction::triggered, this, fn);
        return qobject_cast<QToolButton *>(widgetForAction(a));
    }
};

class DvdMaker : public QWizard
{
    Q_OBJECT
public:
    enum Pages { IntroPage, AddPage, BurnPage, TextPage, StylePage, SlidesPage, PreviewPage };

    DvdMaker()
    {
        setWindowTitle(tr7("Windows DVD Maker"));
        setWindowIcon(QIcon::fromTheme(QLatin1String(kApp)));
        setOption(QWizard::NoBackButtonOnStartPage, true);
        setOption(QWizard::NoCancelButtonOnLastPage, false);
        setOption(QWizard::HaveCustomButton1, true);
        setOption(QWizard::HaveCustomButton2, true);
        setButtonText(QWizard::NextButton, tr7("&Next"));
        setButtonText(QWizard::CancelButton, tr7("Cancel"));
        setButtonText(QWizard::BackButton, tr7("Back"));
        setButtonLayout({QWizard::Stretch, QWizard::CustomButton1, QWizard::NextButton, QWizard::CustomButton2, QWizard::CancelButton});
        connect(this, &QWizard::customButtonClicked, this, &DvdMaker::customButton);

        QSettings st(QStringLiteral("win7"), QStringLiteral("dvdmaker"));
        // NTSC where the TVs are NTSC (the Americas north of the equator, Japan, Korea, Taiwan, the Philippines), else PAL
        static const QList<QLocale::Territory> ntsc{QLocale::UnitedStates, QLocale::Canada, QLocale::Mexico, QLocale::Japan, QLocale::SouthKorea,
                                                    QLocale::Taiwan, QLocale::Philippines, QLocale::Cuba, QLocale::Guatemala, QLocale::Honduras,
                                                    QLocale::ElSalvador, QLocale::Nicaragua, QLocale::CostaRica, QLocale::Panama, QLocale::DominicanRepublic,
                                                    QLocale::Colombia, QLocale::Venezuela, QLocale::Peru, QLocale::Ecuador, QLocale::Bolivia, QLocale::Chile};
        const QString region = qEnvironmentVariable("LC_MEASUREMENT", qEnvironmentVariable("LC_ALL", qEnvironmentVariable("LANG")));
        m_s.pal = st.value(QStringLiteral("PAL"), !ntsc.contains(QLocale(region.section(QLatin1Char('.'), 0, 0)).territory())).toBool();
        m_s.wide = st.value(QStringLiteral("Wide"), false).toBool();
        m_s.playback = Settings::Playback(st.value(QStringLiteral("Playback"), 0).toInt());
        m_s.speed = st.value(QStringLiteral("Speed"), 0).toInt();
        m_s.tempDir = st.value(QStringLiteral("Temp")).toString();
        setStyle(m_s.styleGuid);

        buildIntro();
        buildAdd();
        buildBurn();
        buildText();
        buildStylePage();
        buildSlides();
        buildPreview();
        setStartId(st.value(QStringLiteral("SkipIntro"), false).toBool() ? AddPage : IntroPage);
        connect(this, &QWizard::currentIdChanged, this, &DvdMaker::pageChanged);
        Aero::Wizard::convert(this);
        resize(760, 600);
        pageChanged(startId());
    }

    void addFiles(const QStringList &files)
    {
        m_list->addFiles(files);
    }

protected:
    int nextId() const override
    {
        if (m_jumped >= 0) { // a sub page of "Ready to burn DVD"
            return m_jumped;
        }
        switch (currentId()) {
        case IntroPage:
            return AddPage;
        case AddPage:
            return BurnPage;
        default:
            return -1;
        }
    }

private:
    // ---------------------------------------------------------------- pages
    QWizardPage *page(int id, const QString &instruction, QVBoxLayout *&v)
    {
        auto *p = new QWizardPage;
        p->setTitle(instruction);
        v = new QVBoxLayout(p);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(6);
        setPage(id, p);
        return p;
    }

    void buildIntro()
    {
        QVBoxLayout *v;
        page(IntroPage, QString(), v);
        auto *pic = new QLabel;
        pic->setPixmap(res(QStringLiteral("intro")));
        pic->setAlignment(Qt::AlignCenter);
        v->addSpacing(20);
        v->addWidget(pic);
        auto *head = new QLabel(tr7("Turn your digital memories into a DVD"));
        QFont hf = head->font();
        hf.setPointSizeF(hf.pointSizeF() * 12 / 9);
        head->setFont(hf);
        head->setStyleSheet(QStringLiteral("color: #003399;"));
        head->setAlignment(Qt::AlignCenter);
        v->addWidget(head);
        auto *text = new QLabel(tr7("Make a DVD-Video disc that includes your favorite videos and digital photos that you and others can watch on a TV or computer. Windows DVD Maker lets you create a DVD quickly, complete with professional-looking menus, a scene selection page, and even a slide show with music."));
        text->setWordWrap(true);
        text->setAlignment(Qt::AlignCenter);
        text->setContentsMargins(60, 0, 60, 0);
        v->addWidget(text);
        auto *choose = new QPushButton(tr7("Choose &Photos and Videos"));
        connect(choose, &QPushButton::clicked, this, [this]() { next(); });
        auto *row = new QHBoxLayout;
        row->addStretch(1);
        row->addWidget(choose);
        row->addStretch(1);
        v->addLayout(row);
        v->addStretch(1);
        auto *dont = new QCheckBox(tr7("&Don't show this page again"));
        connect(dont, &QCheckBox::toggled, this, [](bool on) { QSettings(QStringLiteral("win7"), QStringLiteral("dvdmaker")).setValue(QStringLiteral("SkipIntro"), on); });
        v->addWidget(dont);
    }

    void buildAdd()
    {
        QVBoxLayout *v;
        page(AddPage, tr7("Add pictures and video to the DVD"), v);
        auto *band = new Band(this);
        auto *file = new QToolButton;
        file->setText(tr7("File"));
        file->setPopupMode(QToolButton::InstantPopup);
        auto *fileMenu = new QMenu(file);
        fileMenu->addAction(tr7("Exit"), this, &QWidget::close);
        file->setMenu(fileMenu);
        band->addWidget(file);
        band->addSeparator();
        band->button(tr7("Add items"), stripIcon(1), [this]() { addItems(); });
        m_remove = band->button(tr7("Remove items"), stripIcon(2), [this]() { removeItems(); });
        band->addSeparator();
        m_up = band->button(QString(), stripIcon(3), [this]() { move(-1); });
        m_up->setToolTip(tr7("Move the selected item up in the list"));
        m_down = band->button(QString(), stripIcon(4), [this]() { move(1); });
        m_down->setToolTip(tr7("Move the selected item down in the list"));
        auto *spacer = new QWidget;
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        band->addWidget(spacer);
        // the burner (when there is more than one)
        m_burner = new QComboBox;
        m_burner->addItems(burners());
        if (m_burner->count() > 1) {
            auto *bl = new QLabel(tr7("&DVD burner:"));
            bl->setBuddy(m_burner);
            band->addWidget(bl);
            band->addWidget(m_burner);
        }
        v->addWidget(band);

        m_list = new ItemList(this);
        connect(m_list, &ItemList::changed, this, &DvdMaker::updateAdd);
        connect(m_list, &QTreeWidget::itemSelectionChanged, this, &DvdMaker::updateAdd);
        v->addWidget(m_list, 1);

        auto *bottom = new QHBoxLayout;
        m_capIcon = new QLabel;
        m_capIcon->setPixmap(res(QStringLiteral("disc")).scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        bottom->addWidget(m_capIcon);
        m_capacity = new QLabel;
        bottom->addWidget(m_capacity);
        bottom->addStretch(1);
        auto *titleLabel = new QLabel(tr7("DVD &title:"));
        bottom->addWidget(titleLabel);
        m_title = new QLineEdit(QLocale().toString(QDate::currentDate(), tr7("MMMM d, yyyy")));
        titleLabel->setBuddy(m_title);
        m_title->setMaxLength(32);
        m_title->setFixedWidth(200);
        bottom->addWidget(m_title);
        bottom->addSpacing(12);
        auto *opts = new QLabel(QStringLiteral("<a href=\"#\" style=\"color:#0066cc; text-decoration:none\">%1</a>").arg(tr7("Options")));
        connect(opts, &QLabel::linkActivated, this, &DvdMaker::options);
        bottom->addWidget(opts);
        v->addLayout(bottom);
        updateAdd();
    }

    void buildBurn()
    {
        QVBoxLayout *v;
        page(BurnPage, tr7("Ready to burn DVD"), v);
        auto *band = new Band(this);
        auto *file = new QToolButton;
        file->setText(tr7("File"));
        file->setPopupMode(QToolButton::InstantPopup);
        auto *fileMenu = new QMenu(file);
        fileMenu->addAction(tr7("Exit"), this, &QWidget::close);
        file->setMenu(fileMenu);
        band->addWidget(file);
        band->addSeparator();
        band->button(tr7("Preview"), stripIcon(5), [this]() { showSubPage(PreviewPage); });
        band->button(tr7("Menu text"), stripIcon(6), [this]() { showSubPage(TextPage); });
        band->button(tr7("Customize menu"), stripIcon(7), [this]() { showSubPage(StylePage); });
        band->button(tr7("Slide show"), stripIcon(9), [this]() { showSubPage(SlidesPage); });
        v->addWidget(band);

        auto *row = new QHBoxLayout;
        m_preview = new QLabel;
        m_preview->setAlignment(Qt::AlignCenter);
        m_preview->setMinimumSize(420, 315);
        m_preview->setStyleSheet(QStringLiteral("background: black;"));
        row->addWidget(m_preview, 1);
        auto *side = new QVBoxLayout;
        auto *styles = new QComboBox;
        styles->addItem(tr7("Menu Styles"));
        side->addWidget(styles);
        m_styleList = new QListWidget;
        m_styleList->setViewMode(QListView::ListMode);
        m_styleList->setIconSize(QSize(120, 90));
        m_styleList->setFixedWidth(170);
        m_styleList->setSpacing(4);
        for (const Style &s : Styles::instance().styles) {
            QImage thumb = styleImage(s.thumbnail);
            QPixmap pm(124, 94);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.drawImage(QRect(2, 2, 120, 90), thumb);
            p.end();
            auto *it = new QListWidgetItem(QIcon(pm), tr7(s.name.toUtf8().constData()));
            it->setData(Qt::UserRole, s.guid);
            it->setTextAlignment(Qt::AlignHCenter);
            m_styleList->addItem(it);
        }
        m_styleList->setViewMode(QListView::IconMode);
        m_styleList->setFlow(QListView::LeftToRight);
        m_styleList->setWrapping(true);
        m_styleList->setResizeMode(QListView::Adjust);
        m_styleList->setGridSize(QSize(150, 120));
        connect(m_styleList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *it) {
            if (it) {
                setStyle(it->data(Qt::UserRole).toString());
                renderPreview();
            }
        });
        side->addWidget(m_styleList, 1);
        row->addLayout(side);
        v->addLayout(row, 1);
    }

    // Menu text: font, title, button texts and notes, with the previews of the main and notes menus
    void buildText()
    {
        QVBoxLayout *v;
        page(TextPage, tr7("Change the DVD menu text"), v);
        auto *row = new QHBoxLayout;
        auto *form = new QFormLayout;
        m_font = new QFontComboBox;
        form->addRow(tr7("&Font:"), m_font);
        m_tTitle = new QLineEdit;
        m_tPlay = new QLineEdit;
        m_tScenes = new QLineEdit;
        m_tNotesBtn = new QLineEdit;
        m_tNotes = new QPlainTextEdit;
        m_tNotes->setPlaceholderText(tr7("Add your notes here."));
        m_tNotes->setFixedHeight(90);
        for (QLineEdit *e : {m_tTitle, m_tPlay, m_tScenes, m_tNotesBtn}) {
            e->setMaxLength(32);
        }
        form->addRow(tr7("DVD &title:"), m_tTitle);
        form->addRow(tr7("&Play button:"), m_tPlay);
        form->addRow(tr7("&Scenes button:"), m_tScenes);
        form->addRow(tr7("Notes b&utton:"), m_tNotesBtn);
        form->addRow(tr7("N&otes:"), m_tNotes);
        row->addLayout(form, 1);
        auto *pv = new QVBoxLayout;
        m_textPreview = new QLabel;
        m_textPreview->setFixedSize(240, 180);
        m_textPreview->setStyleSheet(QStringLiteral("background: black;"));
        pv->addWidget(m_textPreview);
        m_notesPreview = new QLabel;
        m_notesPreview->setFixedSize(240, 180);
        m_notesPreview->setStyleSheet(QStringLiteral("background: black;"));
        pv->addWidget(m_notesPreview);
        pv->addStretch(1);
        row->addLayout(pv);
        v->addLayout(row, 1);
        auto update = [this]() {
            Texts t = editedTexts();
            m_textPreview->setPixmap(QPixmap::fromImage(menuImage(QStringLiteral("MainMenu"), t).scaled(240, 180, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
            m_notesPreview->setPixmap(QPixmap::fromImage(menuImage(QStringLiteral("NotesMenu"), t).scaled(240, 180, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
        };
        for (QLineEdit *e : {m_tTitle, m_tPlay, m_tScenes, m_tNotesBtn}) {
            connect(e, &QLineEdit::textChanged, this, update);
        }
        connect(m_tNotes, &QPlainTextEdit::textChanged, this, update);
        connect(m_font, &QFontComboBox::currentFontChanged, this, update);
        m_updateTextPreview = update;
    }

    // Customize menu: foreground / background video, menu audio
    void buildStylePage()
    {
        QVBoxLayout *v;
        page(StylePage, tr7("Customize the DVD menu style"), v);
        auto *form = new QFormLayout;
        auto browse = [this](QLineEdit *e, const QString &title, const QString &filter) {
            auto *w = new QWidget;
            auto *h = new QHBoxLayout(w);
            h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(e, 1);
            auto *b = new QPushButton(tr7("B&rowse..."));
            connect(b, &QPushButton::clicked, this, [this, e, title, filter]() {
                const QString f = QFileDialog::getOpenFileName(this, title, QString(), filter);
                if (!f.isEmpty()) {
                    e->setText(f);
                }
            });
            h->addWidget(b);
            return w;
        };
        m_fg = new QLineEdit;
        m_bg = new QLineEdit;
        m_audio = new QLineEdit;
        const QString media = tr7("Video or Picture Files") + QStringLiteral(" (*.") + (kVideo + kPicture).join(QStringLiteral(" *.")) + QLatin1Char(')');
        const QString audio = tr7("Audio Files") + QStringLiteral(" (*.") + kAudio.join(QStringLiteral(" *.")) + QLatin1Char(')');
        form->addRow(tr7("Foreground &video:"), browse(m_fg, tr7("Add Foreground Video"), media));
        form->addRow(tr7("Bac&kground video:"), browse(m_bg, tr7("Add Background Video"), media));
        form->addRow(tr7("Menu &audio:"), browse(m_audio, tr7("Add Audio to the Menu"), audio));
        v->addLayout(form);
        m_stylePreview = new QLabel;
        m_stylePreview->setMinimumSize(360, 270);
        m_stylePreview->setAlignment(Qt::AlignCenter);
        m_stylePreview->setStyleSheet(QStringLiteral("background: black;"));
        v->addWidget(m_stylePreview, 1);
    }

    // Slide show: music, picture length, transition, pan and zoom
    void buildSlides()
    {
        QVBoxLayout *v;
        page(SlidesPage, tr7("Change your slide show settings"), v);
        v->addWidget(new QLabel(tr7("&Music for slide show:")));
        auto *row = new QHBoxLayout;
        m_music = new QTreeWidget;
        m_music->setHeaderLabels({tr7("Song"), tr7("Length")});
        m_music->setRootIsDecorated(false);
        m_music->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        row->addWidget(m_music, 1);
        auto *btns = new QVBoxLayout;
        auto *add = new QPushButton(tr7("&Add Music..."));
        auto *up = new QPushButton(tr7("Move &Up"));
        auto *down = new QPushButton(tr7("Move D&own"));
        auto *rem = new QPushButton(tr7("&Remove"));
        for (QPushButton *b : {add, up, down, rem}) {
            btns->addWidget(b);
        }
        btns->addStretch(1);
        row->addLayout(btns);
        v->addLayout(row, 1);
        connect(add, &QPushButton::clicked, this, [this]() {
            const QStringList files = QFileDialog::getOpenFileNames(this, tr7("Add Music to Slide Show"), QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
                                                                    tr7("Audio Files") + QStringLiteral(" (*.") + kAudio.join(QStringLiteral(" *.")) + QLatin1Char(')'));
            for (const QString &f : files) {
                auto *it = new QTreeWidgetItem(m_music, {QFileInfo(f).completeBaseName(), duration(probeDuration(f))});
                it->setData(0, Qt::UserRole, f);
            }
            updateSlideLengths();
        });
        connect(rem, &QPushButton::clicked, this, [this]() {
            qDeleteAll(m_music->selectedItems());
            updateSlideLengths();
        });
        auto moveSel = [this](int d) {
            QTreeWidgetItem *it = m_music->currentItem();
            if (!it) {
                return;
            }
            const int i = m_music->indexOfTopLevelItem(it);
            const int j = i + d;
            if (j < 0 || j >= m_music->topLevelItemCount()) {
                return;
            }
            m_music->takeTopLevelItem(i);
            m_music->insertTopLevelItem(j, it);
            m_music->setCurrentItem(it);
        };
        connect(up, &QPushButton::clicked, this, [moveSel]() { moveSel(-1); });
        connect(down, &QPushButton::clicked, this, [moveSel]() { moveSel(1); });
        auto *lengths = new QFormLayout;
        m_musicLen = new QLabel(QStringLiteral("0:00:00"));
        m_slideLen = new QLabel(QStringLiteral("0:00:00"));
        lengths->addRow(tr7("Music length:"), m_musicLen);
        lengths->addRow(tr7("Slide show:"), m_slideLen);
        v->addLayout(lengths);
        m_match = new QCheckBox(tr7("Change &slide show length to match music length"));
        v->addWidget(m_match);
        auto *form = new QFormLayout;
        m_picLen = new QComboBox;
        for (int s : {3, 4, 5, 6, 7, 8, 10, 15, 20, 30}) {
            m_picLen->addItem(QString::number(s), s);
        }
        m_picLen->setCurrentIndex(4);
        auto *picRow = new QWidget;
        auto *ph = new QHBoxLayout(picRow);
        ph->setContentsMargins(0, 0, 0, 0);
        ph->addWidget(m_picLen);
        ph->addWidget(new QLabel(tr7("seconds")));
        ph->addStretch(1);
        form->addRow(tr7("&Picture length:"), picRow);
        m_transition = new QComboBox;
        for (const char *t : {"Cross fade", "Cut", "Dissolve", "Flip", "Inset", "Page curl", "Pixelate", "Random", "Wipe"}) {
            m_transition->addItem(tr7(t), QLatin1String(t));
        }
        form->addRow(tr7("&Transition:"), m_transition);
        v->addLayout(form);
        m_panZoom = new QCheckBox(tr7("Use pan and &zoom effects for pictures"));
        v->addWidget(m_panZoom);
        connect(m_picLen, &QComboBox::currentIndexChanged, this, &DvdMaker::updateSlideLengths);
    }

    // Preview: the menus as the DVD shows them; Play / Scenes / Notes work
    void buildPreview()
    {
        QVBoxLayout *v;
        page(PreviewPage, tr7("Preview your DVD"), v);
        m_previewBig = new QLabel;
        m_previewBig->setAlignment(Qt::AlignCenter);
        m_previewBig->setMinimumSize(480, 360);
        m_previewBig->setStyleSheet(QStringLiteral("background: black;"));
        m_previewBig->installEventFilter(this);
        v->addWidget(m_previewBig, 1);
        auto *controls = new QHBoxLayout;
        controls->addStretch(1);
        auto *menuBtn = new QToolButton;
        menuBtn->setIcon(QIcon(res(QStringLiteral("pv-menu"))));
        menuBtn->setToolTip(tr7("Show the DVD Menu"));
        connect(menuBtn, &QToolButton::clicked, this, [this]() { showPreviewMenu(QStringLiteral("MainMenu"), 0); });
        controls->addWidget(menuBtn);
        controls->addStretch(1);
        v->addLayout(controls);
    }

    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == m_previewBig && e->type() == QEvent::MouseButtonPress) {
            const QPoint pos = static_cast<QMouseEvent *>(e)->pos();
            const QPixmap pm = m_previewBig->pixmap();
            if (pm.isNull()) {
                return false;
            }
            const QPoint origin((m_previewBig->width() - pm.width()) / 2, (m_previewBig->height() - pm.height()) / 2);
            const QSize frame = Burner(m_list->items, m_s, QString()).frameSize();
            const QPointF p((pos - origin).x() * double(frame.width()) / pm.width(), (pos - origin).y() * double(frame.height()) / pm.height());
            for (const MenuButton &b : std::as_const(m_previewButtons)) {
                if (b.rect.contains(p.toPoint())) {
                    previewClick(b.name);
                    return true;
                }
            }
        }
        return QWizard::eventFilter(o, e);
    }

    void previewClick(const QString &name)
    {
        if (name == QLatin1String("PlayButton") || name.startsWith(QLatin1String("Scene"))) {
            const auto titles = Burner::titles(m_list->items);
            const int n = name.startsWith(QLatin1String("Scene")) ? m_previewPage * 6 + sceneNumber(name) - 1 : 0;
            if (n < titles.size() && !titles[n].isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(titles[n].first().path));
            }
        } else if (name == QLatin1String("Submenu1Button")) {
            showPreviewMenu(QStringLiteral("Scenes"), 0);
        } else if (name == QLatin1String("Submenu2Button")) {
            showPreviewMenu(QStringLiteral("NotesMenu"), 0);
        } else if (name == QLatin1String("NextMenuButton")) {
            showPreviewMenu(QStringLiteral("Scenes"), m_previewPage + 1);
        } else if (name == QLatin1String("PreviousMenuButton")) {
            showPreviewMenu(QStringLiteral("Scenes"), m_previewPage - 1);
        } else {
            showPreviewMenu(QStringLiteral("MainMenu"), 0);
        }
    }

    void showPreviewMenu(const QString &which, int page)
    {
        const auto titles = Burner::titles(m_list->items);
        QList<QImage> thumbs;
        for (const auto &t : titles) {
            thumbs << Burner::titleThumb(t);
        }
        MenuContent c = content(m_s.texts);
        QString pageName = which;
        m_previewPage = 0;
        if (which == QLatin1String("Scenes")) {
            const int pages = qMax(1, int((thumbs.size() + 5) / 6));
            m_previewPage = qBound(0, page, pages - 1);
            c.scenes = thumbs.mid(m_previewPage * 6, 6);
            c.hasPrevious = m_previewPage > 0;
            c.hasNext = m_previewPage < pages - 1;
            pageName = c.scenes.size() <= 2 ? QStringLiteral("ScenesMenu2") : c.scenes.size() <= 4 ? QStringLiteral("ScenesMenu4") : QStringLiteral("ScenesMenu6");
        }
        const Style *s = Styles::instance().find(m_s.styleGuid);
        if (!s) {
            return;
        }
        const QImage img = Renderer::render(*s, pageName, c, Burner(m_list->items, m_s, QString()).frameSize(), &m_previewButtons, nullptr);
        m_previewBig->setPixmap(QPixmap::fromImage(displayShape(img)).scaled(m_previewBig->size() - QSize(8, 8), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    // ---------------------------------------------------------------- behaviour
    void setStyle(const QString &guid)
    {
        m_s.styleGuid = guid;
        if (const Style *s = Styles::instance().find(guid)) {
            const QString title = m_s.texts.title;
            const bool custom = m_textsEdited;
            if (!custom) {
                m_s.texts = s->defaults;
                m_s.texts.title = title.isEmpty() ? s->defaults.title : title;
                m_s.texts.notesText.clear(); // "Add your notes here." is the editor's hint: no notes, no Notes button
            }
        }
    }

    MenuContent content(const Texts &t)
    {
        MenuContent c;
        c.texts = t;
        if (m_insetFor != m_list->items.size() || m_insetCache.isNull()) {
            m_insetCache = Burner::contentPicture(m_list->items);
            m_insetFor = m_list->items.size();
        }
        c.inset = m_s.foreground.isEmpty() ? m_insetCache : QImage(m_s.foreground);
        c.background = m_s.background.isEmpty() ? c.inset : QImage(m_s.background);
        c.hasNotes = !t.notesText.isEmpty();
        return c;
    }

    /** the DVD frame shown in its 4:3 or 16:9 shape */
    QImage displayShape(const QImage &img) const
    {
        return img.scaled(m_s.wide ? img.height() * 16 / 9 : img.height() * 4 / 3, img.height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    QImage menuImage(const QString &page, const Texts &t)
    {
        const Style *s = Styles::instance().find(m_s.styleGuid);
        if (!s) {
            return {};
        }
        return displayShape(Renderer::render(*s, page, content(t), QSize(720, m_s.pal ? 576 : 480)));
    }

    void renderPreview()
    {
        m_s.texts.title = m_title->text();
        const QImage img = menuImage(QStringLiteral("MainMenu"), m_s.texts);
        m_preview->setPixmap(QPixmap::fromImage(img).scaled(m_preview->size() - QSize(8, 8), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    Texts editedTexts() const
    {
        Texts t = m_s.texts;
        t.font = m_font->currentFont().family();
        t.title = m_tTitle->text();
        t.play = m_tPlay->text();
        t.scenes = m_tScenes->text();
        t.notes = m_tNotesBtn->text();
        t.notesText = m_tNotes->toPlainText();
        return t;
    }

    void updateAdd()
    {
        const double minutes = m_list->totalMinutes(m_picLen ? m_picLen->currentData().toInt() : 7);
        const int capacity = 150;
        m_capacity->setText(minutes > capacity ? tr7("%1 minutes over disc capacity").arg(int(minutes - capacity + 0.99))
                                               : tr7("%1 of %2 minutes").arg(int(minutes + 0.99)).arg(capacity));
        m_capacity->setStyleSheet(minutes > capacity ? QStringLiteral("color: #c00000;") : QString());
        const bool sel = !m_list->selectedItems().isEmpty();
        m_remove->setEnabled(sel);
        m_up->setEnabled(sel);
        m_down->setEnabled(sel);
        if (QAbstractButton *n = button(QWizard::NextButton)) {
            n->setEnabled(!m_list->items.isEmpty() && minutes <= capacity);
        }
        updateSlideLengths();
    }

    void updateSlideLengths()
    {
        if (!m_music || !m_list) {
            return;
        }
        double music = 0;
        m_s.music.clear();
        for (int i = 0; i < m_music->topLevelItemCount(); ++i) {
            const QString f = m_music->topLevelItem(i)->data(0, Qt::UserRole).toString();
            m_s.music << f;
            music += probeDuration(f);
        }
        m_musicLen->setText(duration(music));
        m_slideLen->setText(duration(m_list->pictureCount() * m_picLen->currentData().toInt()));
    }

    void addItems()
    {
        const QString filter = tr7("Video or Picture Files") + QStringLiteral(" (*.") + (kVideo + kPicture).join(QStringLiteral(" *.")) + QStringLiteral(")");
        const QStringList files = QFileDialog::getOpenFileNames(this, tr7("Add Items to DVD"), QStandardPaths::writableLocation(QStandardPaths::MoviesLocation), filter);
        if (!files.isEmpty()) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            m_list->addFiles(files);
            QApplication::restoreOverrideCursor();
        }
    }

    void removeItems()
    {
        const QList<int> sel = m_list->selectedIndexes2();
        for (int i = sel.size() - 1; i >= 0; --i) {
            m_list->items.removeAt(sel[i]);
        }
        m_list->refresh(m_picLen->currentData().toInt());
        updateAdd();
    }

    void move(int d)
    {
        const QList<int> sel = m_list->selectedIndexes2();
        if (sel.size() != 1) {
            return;
        }
        const int i = sel.first(), j = i + d;
        if (j < 0 || j >= m_list->items.size()) {
            return;
        }
        m_list->items.swapItemsAt(i, j);
        m_list->refresh(m_picLen->currentData().toInt());
        for (int r = 0; r < m_list->topLevelItemCount(); ++r) {
            if (m_list->topLevelItem(r)->data(0, Qt::UserRole).toInt() == j) {
                m_list->topLevelItem(r)->setSelected(true);
            }
        }
        updateAdd();
    }

    // the DVD-Video options
    void options()
    {
        QDialog d(this);
        d.setWindowTitle(tr7("DVD-Video"));
        auto *v = new QVBoxLayout(&d);
        auto *play = new QGroupBox(tr7("Choose DVD playback settings"));
        auto *pv = new QVBoxLayout(play);
        QRadioButton *pb[3] = {new QRadioButton(tr7("&Start with DVD menu")), new QRadioButton(tr7("Play video and &end with DVD menu")),
                               new QRadioButton(tr7("Play video in a &continuous loop"))};
        for (QRadioButton *r : pb) {
            pv->addWidget(r);
        }
        pb[m_s.playback]->setChecked(true);
        v->addWidget(play);
        auto *aspect = new QGroupBox(tr7("DVD aspect ratio"));
        auto *av = new QHBoxLayout(aspect);
        auto *a43 = new QRadioButton(tr7("4:&3"));
        auto *a169 = new QRadioButton(tr7("16:&9"));
        av->addWidget(a43);
        av->addWidget(a169);
        av->addStretch(1);
        (m_s.wide ? a169 : a43)->setChecked(true);
        v->addWidget(aspect);
        auto *fmt = new QGroupBox(tr7("Video format"));
        auto *fv = new QHBoxLayout(fmt);
        auto *ntsc = new QRadioButton(tr7("&NTSC"));
        auto *pal = new QRadioButton(tr7("&PAL"));
        fv->addWidget(ntsc);
        fv->addWidget(pal);
        fv->addStretch(1);
        (m_s.pal ? pal : ntsc)->setChecked(true);
        v->addWidget(fmt);
        auto *form = new QFormLayout;
        auto *speed = new QComboBox;
        speed->addItems({tr7("Fastest"), tr7("Medium"), tr7("Slow")});
        speed->setCurrentIndex(m_s.speed);
        form->addRow(tr7("&DVD burner speed:"), speed);
        auto *temp = new QLineEdit(m_s.tempDir.isEmpty() ? tr7("<Default>") : m_s.tempDir);
        auto *browse = new QPushButton(tr7("&Browse..."));
        connect(browse, &QPushButton::clicked, &d, [&]() {
            const QString dir = QFileDialog::getExistingDirectory(&d, tr7("Temporary file location"));
            if (!dir.isEmpty()) {
                temp->setText(dir);
            }
        });
        auto *tw = new QWidget;
        auto *th = new QHBoxLayout(tw);
        th->setContentsMargins(0, 0, 0, 0);
        th->addWidget(temp, 1);
        th->addWidget(browse);
        form->addRow(tr7("&Temporary file location:"), tw);
        v->addLayout(form);
        auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        box->button(QDialogButtonBox::Ok)->setText(tr7("OK"));
        box->button(QDialogButtonBox::Cancel)->setText(tr7("Cancel"));
        connect(box, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(box, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        v->addWidget(box);
        if (d.exec() != QDialog::Accepted) {
            return;
        }
        m_s.playback = pb[1]->isChecked() ? Settings::PlayThenMenu : pb[2]->isChecked() ? Settings::Loop : Settings::StartWithMenu;
        m_s.wide = a169->isChecked();
        m_s.pal = pal->isChecked();
        m_s.speed = speed->currentIndex();
        m_s.tempDir = temp->text() == tr7("<Default>") ? QString() : temp->text();
        QSettings st(QStringLiteral("win7"), QStringLiteral("dvdmaker"));
        st.setValue(QStringLiteral("PAL"), m_s.pal);
        st.setValue(QStringLiteral("Wide"), m_s.wide);
        st.setValue(QStringLiteral("Playback"), int(m_s.playback));
        st.setValue(QStringLiteral("Speed"), m_s.speed);
        st.setValue(QStringLiteral("Temp"), m_s.tempDir);
    }

    // the sub pages of "Ready to burn DVD" come back with OK-like and "Don't Change" buttons
    void showSubPage(int id)
    {
        m_s.texts.title = m_title->text();
        if (id == TextPage) {
            m_font->setCurrentFont(QFont(m_s.texts.font));
            m_tTitle->setText(m_s.texts.title);
            m_tPlay->setText(m_s.texts.play);
            m_tScenes->setText(m_s.texts.scenes);
            m_tNotesBtn->setText(m_s.texts.notes);
            m_tNotes->setPlainText(m_s.texts.notesText);
            m_updateTextPreview();
        } else if (id == StylePage) {
            m_fg->setText(m_s.foreground);
            m_bg->setText(m_s.background);
            m_audio->setText(m_s.menuAudio);
            m_stylePreview->setPixmap(QPixmap::fromImage(menuImage(QStringLiteral("MainMenu"), m_s.texts)).scaled(480, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else if (id == PreviewPage) {
            QTimer::singleShot(0, this, [this]() { showPreviewMenu(QStringLiteral("MainMenu"), 0); });
        }
        // shown after "Ready to burn DVD": its buttons (and the round back arrow) return there
        m_jumped = id;
        QWizard::next();
        m_jumped = -1;
    }

    void pageChanged(int id)
    {
        auto setBtn = [this](QWizard::WizardButton b, const QString &t, bool show) {
            if (QAbstractButton *x = button(b)) {
                x->setText(t);
                x->setVisible(show);
            }
        };
        const bool sub = id == TextPage || id == StylePage || id == SlidesPage || id == PreviewPage;
        setBtn(QWizard::CustomButton1, id == BurnPage ? tr7("B&urn") : id == TextPage ? tr7("&Change Text") : id == StylePage ? tr7("&Change Style")
                                       : id == SlidesPage                              ? tr7("&Change Slide Show")
                                       : id == PreviewPage                             ? tr7("&OK")
                                                                                       : QString(),
               id == BurnPage || sub);
        setBtn(QWizard::CustomButton2, tr7("&Don't Change"), sub && id != PreviewPage);
        setBtn(QWizard::NextButton, tr7("&Next"), id == AddPage);
        setBtn(QWizard::CancelButton, tr7("Cancel"), !sub);
        if (id == AddPage) {
            updateAdd();
        }
        if (id == BurnPage) {
            QTimer::singleShot(0, this, [this]() {
                if (!m_styleList->currentItem()) {
                    for (int i = 0; i < m_styleList->count(); ++i) {
                        if (m_styleList->item(i)->data(Qt::UserRole).toString() == m_s.styleGuid) {
                            m_styleList->setCurrentRow(i);
                        }
                    }
                }
                renderPreview();
            });
        }
    }

    void customButton(int which)
    {
        const int id = currentId();
        if (id == BurnPage && which == QWizard::CustomButton1) {
            burn();
            return;
        }
        if (which == QWizard::CustomButton1) {
            if (id == TextPage) {
                m_s.texts = editedTexts();
                m_title->setText(m_s.texts.title);
                m_textsEdited = true;
            } else if (id == StylePage) {
                m_s.foreground = m_fg->text();
                m_s.background = m_bg->text();
                m_s.menuAudio = m_audio->text();
            } else if (id == SlidesPage) {
                updateSlideLengths();
                m_s.pictureSeconds = m_picLen->currentData().toInt();
                m_s.transition = m_transition->currentData().toString();
                if (m_s.transition == QLatin1String("Random")) {
                    m_s.transition = QStringLiteral("Cross fade");
                }
                m_s.panZoom = m_panZoom->isChecked();
                if (m_match->isChecked() && m_list->pictureCount() > 0) {
                    double music = 0;
                    for (const QString &f : std::as_const(m_s.music)) {
                        music += probeDuration(f);
                    }
                    m_s.pictureSeconds = qMax(3, int(music / m_list->pictureCount()));
                }
                m_list->refresh(m_s.pictureSeconds);
            }
        }
        back();
    }

    void burn()
    {
        m_s.texts.title = m_title->text();
        if (!qEnvironmentVariableIsEmpty("SEVEN_DVDMAKER_ISO")) { // test hook: the disc as an ISO image
            BurnDialog dlg(m_list->items, m_s, QString(), this);
            dlg.exec();
            return;
        }
        if (m_burner->count() == 0) {
            QMessageBox::warning(this, tr7("DVD burner not found"), tr7("A DVD burner was not found. Make sure a DVD burner is connected and installed, and then try again. "));
            return;
        }
        const QString dev = m_burner->currentText();
        // the disc: Windows DVD Maker asks for one, and before writing over a rewritable one
        for (;;) {
            const Disc d = discState(dev);
            if (d == Disc::Blank) {
                break;
            }
            if (d == Disc::Rewritable) {
                if (QMessageBox::question(this, tr7("Existing data on disc"),
                                          tr7("The disc in the DVD burner (%1) already has content on it. Do you want to overwrite the content that is already on the disc?").arg(dev))
                    != QMessageBox::Yes) {
                    return;
                }
                break;
            }
            const QString text = d == Disc::None ? tr7("Insert a recordable or rewritable DVD, and then click OK")
                                                 : tr7("The DVD in the burner is not a blank or rewritable DVD. Insert a blank or rewritable DVD, and then try again.");
            if (QMessageBox::information(this, d == Disc::None ? tr7("Insert a disc") : tr7("Insert a blank DVD"), text, QMessageBox::Ok | QMessageBox::Cancel)
                != QMessageBox::Ok) {
                return;
            }
        }
        BurnDialog dlg(m_list->items, m_s, dev, this);
        dlg.exec();
    }

    // ---------------------------------------------------------------- burning window
    class BurnDialog : public QDialog
    {
    public:
        BurnDialog(const QList<Item> &items, const Settings &s, const QString &dev, QWidget *parent)
            : QDialog(parent)
            , m_items(items)
            , m_s(s)
            , m_dev(dev)
        {
            setWindowTitle(tr7("Burning DVD..."));
            auto *v = new QVBoxLayout(this);
            m_status = new QLabel(tr7("Creating DVD, please wait..."));
            v->addWidget(m_status);
            m_bar = new QProgressBar;
            m_bar->setRange(0, 100);
            v->addWidget(m_bar);
            m_time = new QLabel(tr7("Time remaining: calculating..."));
            v->addWidget(m_time);
            auto *row = new QHBoxLayout;
            row->addStretch(1);
            m_cancel = new QPushButton(tr7("Cancel"));
            row->addWidget(m_cancel);
            v->addLayout(row);
            setFixedWidth(420);
            connect(m_cancel, &QPushButton::clicked, this, [this]() {
                if (QMessageBox::question(this, tr7("Cancel burning?"), tr7("If you cancel burning now, nothing will be burned to your DVD. Are you sure you want to cancel?"))
                    == QMessageBox::Yes) {
                    m_burner->cancel();
                }
            });
            QTimer::singleShot(0, this, &BurnDialog::start);
        }

    private:
        void start()
        {
            m_burner = new Burner(m_items, m_s, m_dev, this);
            m_clock.start();
            connect(m_burner, &Burner::progress, this, [this](int pct, const QString &status) {
                m_bar->setValue(pct);
                m_status->setText(status);
                const qint64 ms = m_clock.elapsed();
                if (pct >= 3) {
                    const double left = ms / 1000.0 * (100 - pct) / pct / 60.0;
                    m_time->setText(left < 1 ? tr7("Time remaining: less than 1 minute")
                                             : int(left + 0.5) == 1 ? tr7("Time remaining: %1 minute").arg(1)
                                                                    : tr7("Time remaining: %1 minutes").arg(int(left + 0.5)));
                }
            });
            connect(m_burner, &Burner::finished, this, [this](bool ok, const QString &error) {
                if (ok) {
                    hide();
                    QMessageBox done(parentWidget());
                    done.setWindowTitle(tr7("Windows DVD Maker"));
                    done.setText(tr7("Your DVD is ready"));
                    QPushButton *copy = done.addButton(tr7("Make another copy of this DVD"), QMessageBox::ActionRole);
                    done.addButton(tr7("Close"), QMessageBox::AcceptRole);
                    done.exec();
                    if (done.clickedButton() == copy) {
                        show();
                        m_bar->setValue(0);
                        QTimer::singleShot(0, this, &BurnDialog::start);
                        return;
                    }
                    accept();
                    return;
                }
                m_burner->cleanup();
                if (!error.isEmpty()) {
                    QMessageBox::warning(this, tr7("Cannot create the DVD"), error);
                }
                reject();
            });
            m_burner->start();
        }
        QList<Item> m_items;
        Settings m_s;
        QString m_dev;
        Burner *m_burner = nullptr;
        QLabel *m_status, *m_time;
        QProgressBar *m_bar;
        QPushButton *m_cancel;
        QElapsedTimer m_clock;
    };

    Settings m_s;
    ItemList *m_list = nullptr;
    QToolButton *m_remove = nullptr, *m_up = nullptr, *m_down = nullptr;
    QComboBox *m_burner = nullptr;
    QLabel *m_capIcon = nullptr, *m_capacity = nullptr, *m_preview = nullptr, *m_previewBig = nullptr, *m_textPreview = nullptr, *m_notesPreview = nullptr,
           *m_stylePreview = nullptr, *m_musicLen = nullptr, *m_slideLen = nullptr;
    QLineEdit *m_title = nullptr, *m_tTitle = nullptr, *m_tPlay = nullptr, *m_tScenes = nullptr, *m_tNotesBtn = nullptr, *m_fg = nullptr, *m_bg = nullptr, *m_audio = nullptr;
    QPlainTextEdit *m_tNotes = nullptr;
    QFontComboBox *m_font = nullptr;
    QListWidget *m_styleList = nullptr;
    QTreeWidget *m_music = nullptr;
    QComboBox *m_picLen = nullptr, *m_transition = nullptr;
    QCheckBox *m_match = nullptr, *m_panZoom = nullptr;
    std::function<void()> m_updateTextPreview;
    QList<MenuButton> m_previewButtons;
    int m_previewPage = 0;
    int m_jumped = -1;
    bool m_textsEdited = false;
    QImage m_insetCache;
    int m_insetFor = -1;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Win7I18n::install(&app);
    app.setApplicationName(QLatin1String(kApp));
    app.setApplicationDisplayName(tr7("Windows DVD Maker"));
    app.setDesktopFileName(QLatin1String(kApp));

    // test hook: --render STYLE PAGE OUT.png [PICTURE] draws a menu of a style
    const QStringList args = app.arguments();
    const int r = args.indexOf(QStringLiteral("--render"));
    if (r > 0 && args.size() > r + 3) {
        const Style *s = Styles::instance().find(args[r + 1]);
        if (!s) {
            return 1;
        }
        MenuContent c;
        c.texts = s->defaults;
        c.texts.title = QStringLiteral("Our Holiday");
        c.texts.notesText = s->defaults.notesText;
        c.inset = QImage(args.value(r + 4));
        c.background = c.inset;
        for (int i = 0; i < 6; ++i) {
            c.scenes << c.inset;
        }
        c.hasNext = true;
        QList<MenuButton> buttons;
        QImage hl;
        QImage img = Renderer::render(*s, args[r + 2], c, QSize(720, 576), &buttons, &hl);
        QPainter p(&img);
        p.setOpacity(0.6);
        p.drawImage(0, 0, hl);
        p.end();
        img.scaled(768, 576).save(args[r + 3]);
        return 0;
    }

    DvdMaker w;
    QStringList files;
    for (int i = 1; i < args.size(); ++i) {
        if (QFileInfo(args[i]).isFile()) {
            files << args[i];
        }
    }
    w.show();
    if (!files.isEmpty()) {
        w.addFiles(files);
    }
    return app.exec();
}

#include "main.moc"
