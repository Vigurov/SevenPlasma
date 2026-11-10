#include "win7i18n.h"
/*
 * seven-nvidia: the NVIDIA Control Panel of a Windows 7 laptop (Optimus), after the screenshots in
 * ~/Desktop/win7 (10-18): the "Select a Task..." tree, the blue banners, Manage 3D settings with Global and
 * Program settings, Adjust image settings with preview, Set PhysX configuration, the Performance pages and
 * System Information. What it changes on Linux is described in nvbackend.h.
 */
#include "nvwidgets.h"
// before namespace Nv: nvdisplay.h is included inside it
#include "cccbackend.h"
#include <QGroupBox>
#include <QListWidget>
#include <QSpinBox>
#include <array>

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QPainter>

namespace Nv
{
enum Page { ImagePage, Manage3DPage, PhysXPage, DevicePage, SystemPage, PoliciesPage, ResolutionPage, DesktopColorPage, RotatePage, HdcpPage, AudioPage,
            SizePage, MultiPage, VideoColorPage, VideoImagePage, PageCount };

/* ---------------- the state the pages edit; Apply writes it ---------------- */

struct State {
    Values global;
    int globalGpu = GpuAuto;
    QMap<QString, Values> programs; // program id -> its own choices
    QMap<QString, int> programGpu;  // program id -> Gpu
    QMap<QString, QString> exes;    // program id -> process name
    int power = 0;                  // index into the "power" feature
    int imageMode = 1;              // 0 the application decides, 1 advanced, 2 my preference
    int imagePref = 2;              // 0 performance, 1 balanced, 2 quality
    int physx = 0;
    bool osd = false;
    bool runMenu = false;
    bool operator==(const State &) const = default;
};

inline const QString &runMenuPath()
{
    static const QString p = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kio/servicemenus/seven-nvidia-run.desktop");
    return p;
}

inline State loadState(const QList<Program> &installed)
{
    State s;
    QSettings &st = store();
    st.beginGroup(QStringLiteral("Global"));
    for (const QString &k : st.childKeys()) {
        s.global.insert(k, st.value(k).toInt());
    }
    st.endGroup();
    s.globalGpu = st.value(QStringLiteral("GlobalGpu"), GpuAuto).toInt();
    s.imageMode = st.value(QStringLiteral("ImageMode"), 1).toInt();
    s.imagePref = st.value(QStringLiteral("ImagePref"), 2).toInt();
    s.physx = st.value(QStringLiteral("PhysX"), 0).toInt();
    s.osd = st.value(QStringLiteral("GraphicsOSD"), false).toBool();
    st.beginGroup(QStringLiteral("Programs"));
    for (const QString &id : st.childGroups()) {
        st.beginGroup(id);
        Values v;
        for (const QString &k : st.childKeys()) {
            if (k == QLatin1String("exe")) {
                s.exes.insert(QString(id).replace(QLatin1Char('|'), QLatin1Char('/')), st.value(k).toString());
            } else {
                v.insert(k, st.value(k).toInt());
            }
        }
        s.programs.insert(QString(id).replace(QLatin1Char('|'), QLatin1Char('/')), v);
        st.endGroup();
    }
    st.endGroup();
    for (const Program &p : installed) {
        const Gpu g = programGpu(p);
        if (g != GpuGlobal) {
            s.programGpu.insert(p.id, g);
            s.exes.insert(p.id, p.exe);
        }
    }
    // power management: what the GPU does now
    const int mode = powerMizerMode();
    const Feature *pf = feature(QStringLiteral("power"));
    for (int i = 0; i < pf->choices.size(); ++i) {
        if (pf->choices.at(i).value.toInt() == mode) {
            s.power = i;
        }
    }
    s.runMenu = QFile::exists(runMenuPath());
    return s;
}

/** The Desktop menu's "Run with graphics processor" in Dolphin's context menu (for programs and .desktop files) */
inline void setRunMenu(bool on)
{
    if (!on) {
        QFile::remove(runMenuPath());
        return;
    }
    QDir().mkpath(QFileInfo(runMenuPath()).absolutePath());
    QFile f(runMenuPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    f.write(QStringLiteral("[Desktop Entry]\nType=Service\nMimeType=application/x-executable;application/x-desktop;application/x-shellscript;application/vnd.microsoft.portable-executable;\nActions=nvidia;integrated;\nX-KDE-Submenu=Run with graphics processor\nX-KDE-Submenu[uk]=Запустити з графічним процесором\nIcon=nvidia-settings\n\n[Desktop Action nvidia]\nName=High-performance NVIDIA processor\nName[uk]=Високопродуктивний процесор NVIDIA\nIcon=nvidia-settings\nExec=env __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia __VK_LAYER_NV_optimus=NVIDIA_only kioclient exec %f\n\n[Desktop Action integrated]\nName=Integrated graphics\nName[uk]=Інтегрований графічний процесор\nExec=kioclient exec %f\n")
                .toUtf8());
    f.setPermissions(f.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
}

inline QString powerAutostartPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/autostart/seven-nvidia-power.desktop");
}

/** Writes everything; returns an error text or an empty string */
inline QString applyState(const State &s, const State &before, QList<Program> &installed)
{
    QSettings &st = store();
    st.remove(QStringLiteral("Global"));
    st.beginGroup(QStringLiteral("Global"));
    for (auto it = s.global.cbegin(); it != s.global.cend(); ++it) {
        st.setValue(it.key(), it.value());
    }
    st.endGroup();
    st.setValue(QStringLiteral("GlobalGpu"), s.globalGpu);
    st.setValue(QStringLiteral("ImageMode"), s.imageMode);
    st.setValue(QStringLiteral("ImagePref"), s.imagePref);
    st.setValue(QStringLiteral("PhysX"), s.physx);
    st.setValue(QStringLiteral("GraphicsOSD"), s.osd);
    st.remove(QStringLiteral("Programs"));
    st.beginGroup(QStringLiteral("Programs"));
    QMap<QString, QPair<QString, Values>> profiles;
    for (auto it = s.programs.cbegin(); it != s.programs.cend(); ++it) {
        bool any = false;
        for (int v : it.value()) {
            any = any || v >= 0;
        }
        if (!any) {
            continue;
        }
        st.beginGroup(QString(it.key()).replace(QLatin1Char('/'), QLatin1Char('|')));
        st.setValue(QStringLiteral("exe"), s.exes.value(it.key()));
        for (auto v = it.value().cbegin(); v != it.value().cend(); ++v) {
            if (v.value() >= 0) {
                st.setValue(v.key(), v.value());
            }
        }
        st.endGroup();
        profiles.insert(it.key(), {s.exes.value(it.key()), it.value()});
    }
    st.endGroup();
    st.sync();
    if (!writeProfiles(s.global, profiles, s.osd)) {
        return tr7("Could not write the profiles file %1.").arg(profilesPath());
    }

    // preferred graphics processor: per program, then the global choice for games without their own
    for (Program &p : installed) {
        const int want = s.programGpu.value(p.id, GpuGlobal);
        const bool gameForGlobal = want == GpuGlobal && s.globalGpu == GpuNvidia
            && (p.id == QLatin1String("steam.desktop") || QSettings(p.desktopPath, QSettings::IniFormat).value(QStringLiteral("Desktop Entry/Categories")).toString().contains(QLatin1String("Game")));
        const int had = before.programGpu.value(p.id, GpuGlobal);
        if (want != had) {
            setProgramGpu(p, Gpu(want));
        }
        if (want == GpuGlobal) {
            const bool hasGlobal = readLines(p.desktopPath).contains(QStringLiteral("X-Win7NV-Global=1"));
            if (gameForGlobal && !hasGlobal) {
                setProgramGpu(p, GpuNvidia, true);
            } else if (!gameForGlobal && hasGlobal) {
                setProgramGpu(p, GpuGlobal);
            }
        }
    }

    // power management mode, again at every login
    const int mode = feature(QStringLiteral("power"))->choices.value(s.power).value.toInt();
    if (s.power != before.power && !setPowerMizerMode(mode)) {
        return tr7("Could not change the power management mode of the graphics processor.");
    }
    st.setValue(QStringLiteral("PowerMizerMode"), mode);
    st.sync();
    if (mode == 2) {
        QFile::remove(powerAutostartPath());
    } else {
        QDir().mkpath(QFileInfo(powerAutostartPath()).absolutePath());
        QFile f(powerAutostartPath());
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(tr7("[Desktop Entry]\nType=Application\nName=NVIDIA Control Panel: power mode\nExec=%1 --apply-power\nNoDisplay=true\n")
                        .arg(QCoreApplication::applicationFilePath())
                        .toUtf8());
        }
    }
    if (s.runMenu != before.runMenu) {
        setRunMenu(s.runMenu);
    }
    return {};
}

/* ---------------- page frame: banner, intro, content, Description / Typical usage scenarios ---------------- */

class PageFrame : public QWidget
{
public:
    Banner *banner;
    QVBoxLayout *body;
    QLabel *desc;
    QLabel *usage;

    PageFrame(const QString &title, const QString &intro, const QString &icon = QStringLiteral("banner-3d.png"))
    {
        setObjectName(QStringLiteral("nvpage"));
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral("#nvpage { background: white; }"));
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 12);
        v->setSpacing(0);
        banner = new Banner(title, icon);
        v->addWidget(banner);
        auto *in = new QLabel(intro);
        in->setWordWrap(true);
        in->setContentsMargins(43, 6, 12, 0);
        v->addWidget(in);
        auto *content = new QWidget;
        body = new QVBoxLayout(content);
        body->setContentsMargins(13, 14, 24, 0);
        body->setSpacing(0);
        v->addWidget(content);
        v->addSpacing(6);
        auto *d = new QWidget;
        auto *dv = new QVBoxLayout(d);
        dv->setContentsMargins(11, 0, 24, 0);
        dv->setSpacing(4);
        dv->addWidget(new QLabel(tr7("Description:")));
        desc = new QLabel;
        desc->setWordWrap(true);
        desc->setContentsMargins(15, 0, 0, 6);
        dv->addWidget(desc);
        dv->addWidget(new QLabel(tr7("Typical usage scenarios:")));
        usage = new QLabel;
        usage->setWordWrap(true);
        usage->setContentsMargins(15, 0, 0, 0);
        dv->addWidget(usage);
        v->addWidget(d);
        v->addStretch(1);
    }
    /** The bottom texts follow the control under the mouse (property "nvDesc" / "nvUsage") */
    void describe(QWidget *w, const QString &d, const QString &u = {})
    {
        w->setProperty("nvDesc", d);
        w->setProperty("nvUsage", u);
        w->installEventFilter(this);
    }
    void show(const QString &d, const QString &u)
    {
        desc->setText(d);
        usage->setText(u.isEmpty() ? QString() : QStringLiteral("• ") + u);
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::Enter || e->type() == QEvent::FocusIn) {
            show(o->property("nvDesc").toString(), o->property("nvUsage").toString());
        }
        return QWidget::eventFilter(o, e);
    }
};

inline QLabel *boldLabel(const QString &t)
{
    auto *l = new QLabel(t);
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    return l;
}

inline QPushButton *restoreButton()
{
    auto *b = new QPushButton(tr7("Restore"));
    b->setIcon(QIcon(asset(QStringLiteral("restore.png"))));
    b->setIconSize(QSize(16, 16));
    b->setMinimumWidth(80);
    return b;
}

inline QLabel *link(const QString &text, std::function<void()> f)
{
    auto *l = new QLabel(QStringLiteral("<a href=\"#\" style=\"color:#0066cc;\">%1</a>").arg(text.toHtmlEscaped()));
    QObject::connect(l, &QLabel::linkActivated, l, [f] {
        f();
    });
    return l;
}

/** A rounded frame whose top line runs through the middle of its first row (a radio button as its title) */
class TitledBox : public QWidget
{
public:
    std::function<int()> titleHeight;
    std::function<int()> titleEnd; // x where the title text ends

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const double top = (titleHeight ? titleHeight() : 16) / 2.0 + 0.5;
        const double r = 3, x0 = 0.5, x1 = width() - 0.5, y1 = height() - 0.5;
        const double gapEnd = titleEnd ? titleEnd() + 4 : 0;
        QPainterPath path;
        path.moveTo(6, top);
        path.lineTo(x0 + r, top);
        path.quadTo(x0, top, x0, top + r);
        path.lineTo(x0, y1 - r);
        path.quadTo(x0, y1, x0 + r, y1);
        path.lineTo(x1 - r, y1);
        path.quadTo(x1, y1, x1, y1 - r);
        path.lineTo(x1, top + r);
        path.quadTo(x1, top, x1 - r, top);
        path.lineTo(gapEnd, top);
        p.setPen(QColor(0xd5, 0xdf, 0xe5));
        p.drawPath(path);
        return;
        path.moveTo(gapEnd, top);
        path.lineTo(x1 - r, top);
        path.quadTo(x1, top, x1, top + r);
        path.lineTo(x1, y1 - r);
        path.quadTo(x1, y1, x1 - r, y1);
        path.lineTo(x0 + r, y1);
        path.quadTo(x0, y1, x0, y1 - r);
        path.lineTo(x0, top + 9); // the radio button covers the corner
        p.setPen(QColor(0xd5, 0xdf, 0xe5));
        p.drawPath(path);
    }
};

/* ---------------- the window ---------------- */

#include "nvdisplay.h"

class Window : public QMainWindow
{
public:
    Window()
    {
        setWindowTitle(tr7("NVIDIA Control Panel"));
        QIcon icon;
        for (int sz : {16, 32, 48, 64, 96, 128, 256}) {
            icon.addFile(asset(QStringLiteral("app%1.png").arg(sz)), QSize(sz, sz));
        }
        setWindowIcon(icon);
        m_programs = installedPrograms();
        m_saved = loadState(m_programs);
        m_state = m_saved;

        buildMenus();
        buildToolBar();

        auto *split = new QSplitter;
        split->setChildrenCollapsible(false);
        split->setHandleWidth(5);
        split->setStyleSheet(QStringLiteral("QSplitter { background: #f0f0f0; } QSplitter::handle { background: #f0f0f0; }"));
        // left: "Select a Task..."
        auto *left = new QWidget;
        // Windows 7: the task pane is a grey (COLOR_3DFACE) area with the tree in a white bordered box
        left->setObjectName(QStringLiteral("nvTaskPane"));
        left->setAttribute(Qt::WA_StyledBackground, true);
        left->setStyleSheet(QStringLiteral("#nvTaskPane { background: #f0f0f0; }"));
        auto *lv = new QVBoxLayout(left);
        lv->setContentsMargins(2, 4, 0, 6);
        lv->setSpacing(3);
        lv->addWidget(new QLabel(tr7("Select a Task...")));
        m_tree = new TaskTree({{tr7("3D Settings")},
                               {tr7("Adjust image settings with preview"), ImagePage},
                               {tr7("Manage 3D settings"), Manage3DPage},
                               {tr7("Set PhysX Configuration"), PhysXPage},
                               {tr7("Display")},
                               {tr7("Change resolution"), ResolutionPage},
                               {tr7("Adjust desktop color settings"), DesktopColorPage},
                               {tr7("Rotate display"), RotatePage},
                               {tr7("View HDCP status"), HdcpPage},
                               {tr7("Set up digital audio"), AudioPage},
                               {tr7("Adjust desktop size and position"), SizePage},
                               {tr7("Set up multiple displays"), MultiPage},
                               {tr7("Performance")},
                               {tr7("Device Settings"), DevicePage},
                               {tr7("View system information"), SystemPage},
                               {tr7("Profile policies"), PoliciesPage},
                               {tr7("Video")},
                               {tr7("Adjust video color settings"), VideoColorPage},
                               {tr7("Adjust video image settings"), VideoImagePage}});
        auto *treeFrame = new QFrame;
        treeFrame->setObjectName(QStringLiteral("nvTreeFrame"));
        treeFrame->setStyleSheet(QStringLiteral("#nvTreeFrame { background: white; border: 1px solid #828790; }"));
        auto *tf = new QVBoxLayout(treeFrame);
        tf->setContentsMargins(1, 1, 1, 1);
        tf->addWidget(m_tree, 1);
        lv->addWidget(treeFrame, 1);
        auto *si = new QHBoxLayout;
        si->setContentsMargins(8, 6, 0, 0);
        si->setSpacing(4);
        auto *siIcon = new QLabel;
        siIcon->setPixmap(QPixmap(asset(QStringLiteral("sysinfo.png"))));
        si->addWidget(siIcon);
        si->addWidget(link(tr7("System Information"), [this] {
            systemInformation();
        }));
        si->addStretch(1);
        lv->addLayout(si);
        left->setMinimumWidth(200);
        split->addWidget(left);

        // right: the page and the Apply / Cancel band
        auto *right = new QWidget;
        auto *rv = new QVBoxLayout(right);
        rv->setContentsMargins(0, 0, 0, 0);
        rv->setSpacing(0);
        m_stack = new QStackedWidget;
        for (int i = 0; i < PageCount; ++i) {
            auto *scroll = new QScrollArea;
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::StyledPanel);
            scroll->setWidget(buildPage(i));
            m_stack->addWidget(scroll);
        }
        rv->addWidget(m_stack, 1);
        m_applyBar = new QWidget;
        auto *ab = new QHBoxLayout(m_applyBar);
        ab->setContentsMargins(0, 8, 14, 2);
        ab->addStretch(1);
        auto *apply = new QPushButton(tr7("Apply"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        for (QPushButton *b : {apply, cancel}) {
            b->setMinimumSize(110, 28);
            ab->addWidget(b);
        }
        ab->setSpacing(24);
        rv->addWidget(m_applyBar);
        m_applyBar->setVisible(false);
        connect(apply, &QPushButton::clicked, this, [this] {
            applyChanges();
        });
        connect(cancel, &QPushButton::clicked, this, [this] {
            m_state = m_saved;
            m_disp.cancel();
            refresh();
        });
        split->addWidget(right);
        split->setStretchFactor(1, 1);
        split->setSizes({290, 690});
        // the panel's pages are in Segoe UI 8 pt on Windows (the menus keep the system font)
        QFont small = font();
        small.setPointSize(8);
        split->setFont(small);
        setCentralWidget(split);

        m_tree->activated = [this](int page) {
            go(page);
        };
        m_disp.changed = [this] {
            if (!m_refreshing) {
                refresh();
            }
        };
        go(Manage3DPage, false);
        refresh();
        resize(1060, 740);
    }

    void go(int page, bool record = true)
    {
        if (page == SystemPage) {
            systemInformation();
            return;
        }
        if (record && page != m_page) {
            m_back.append(m_page);
            m_forward.clear();
        }
        m_page = page;
        m_stack->setCurrentIndex(page);
        m_tree->select(page);
        updateNav();
    }

private:
    /* ---------- menus and toolbar ---------- */
    void buildMenus()
    {
        QMenu *file = menuBar()->addMenu(tr7("&File"));
        file->addAction(tr7("E&xit"), this, &QWidget::close);
        QMenu *edit = menuBar()->addMenu(tr7("&Edit"));
        m_undo = edit->addAction(tr7("&Undo Changes"), this, [this] {
            m_state = m_saved;
            m_disp.cancel();
            refresh();
        });
        m_applyAct = edit->addAction(tr7("&Apply"), this, [this] {
            applyChanges();
        });
        QMenu *desk = menuBar()->addMenu(tr7("&Desktop"));
        m_runMenu = desk->addAction(tr7("Add \"Run with graphics processor\" to Context Menu"));
        m_runMenu->setCheckable(true);
        connect(m_runMenu, &QAction::toggled, this, [this](bool on) {
            m_state.runMenu = on;
            changed();
        });
        QMenu *d3 = menuBar()->addMenu(tr7("3D &Settings"));
        m_osd = d3->addAction(tr7("Show Visual Indicator for graphics"));
        m_osd->setCheckable(true);
        connect(m_osd, &QAction::toggled, this, [this](bool on) {
            m_state.osd = on;
            changed();
        });
        QMenu *help = menuBar()->addMenu(tr7("&Help"));
        help->addAction(tr7("System &Information"), this, [this] {
            systemInformation();
        });
        help->addSeparator();
        help->addAction(tr7("&About"), this, [this] {
            QMessageBox::about(this, tr7("About NVIDIA Control Panel"),
                               tr7("<b>NVIDIA Control Panel</b><br>Driver version: %1<br><br>3D settings are written to the application profiles of the NVIDIA driver (%2).")
                                   .arg(smi({QStringLiteral("driver_version")}).value(0), profilesPath().toHtmlEscaped()));
        });
    }

    void buildToolBar()
    {
        auto *bar = new QToolBar;
        bar->setMovable(false);
        bar->setFloatable(false);
        bar->setIconSize(QSize(24, 24));
        bar->setStyleSheet(QStringLiteral("QToolBar { background: #f0f0f0; border: none; border-bottom: 1px solid #d5d5d5; padding: 3px 4px; spacing: 2px; }"
                                          "QToolBar::separator { width: 1px; background: #a0a0a0; margin: 2px 7px; }"
                                          "QToolButton { border: 1px solid transparent; border-radius: 3px; padding: 1px; color: #36302d; }"
                                          "QToolButton:hover { border-color: #a8c4e6; background: #e6f0fa; }"));
        m_backBtn = new QToolButton;
        m_backBtn->setText(tr7("Back"));
        m_backBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        // Windows: the history arrow is a separate little button after "Back"
        m_backMenu = new QMenu(m_backBtn);
        m_backArrow = new QToolButton;
        m_backArrow->setArrowType(Qt::NoArrow);
        m_backArrow->setFixedSize(12, 26);
        m_backArrow->setMenu(m_backMenu);
        m_backArrow->setPopupMode(QToolButton::InstantPopup);
        m_backArrow->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0; }"));
        m_backArrow->setIcon(QIcon(QPixmap::fromImage([] {
            QImage a(7, 4, QImage::Format_ARGB32_Premultiplied);
            a.fill(Qt::transparent);
            QPainter p(&a);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x30, 0x30, 0x30));
            p.drawPolygon(QPolygonF({QPointF(0, 0), QPointF(7, 0), QPointF(3.5, 4)}));
            return a;
        }())));
        m_backArrow->setIconSize(QSize(7, 4));
        connect(m_backBtn, &QToolButton::clicked, this, [this] {
            if (!m_back.isEmpty()) {
                m_forward.append(m_page);
                go(m_back.takeLast(), false);
            }
        });
        m_fwdBtn = new QToolButton;
        connect(m_fwdBtn, &QToolButton::clicked, this, [this] {
            if (!m_forward.isEmpty()) {
                m_back.append(m_page);
                go(m_forward.takeLast(), false);
            }
        });
        auto *home = new QToolButton;
        home->setIcon(QIcon(asset(QStringLiteral("home.png"))));
        home->setToolTip(tr7("Home"));
        connect(home, &QToolButton::clicked, this, [this] {
            go(Manage3DPage);
        });
        bar->addWidget(m_backBtn);
        bar->addWidget(m_backArrow);
        bar->addWidget(m_fwdBtn);
        bar->addSeparator();
        bar->addWidget(home);
        addToolBar(bar);
    }

    void updateNav()
    {
        m_backBtn->setEnabled(!m_back.isEmpty());
        m_backArrow->setEnabled(!m_back.isEmpty());
        // the disabled picture as it is (Qt would lighten it once more)
        auto icon = [](const QString &file) {
            QIcon i;
            i.addFile(asset(file), {}, QIcon::Normal);
            i.addFile(asset(file), {}, QIcon::Disabled);
            return i;
        };
        m_backBtn->setIcon(icon(m_back.isEmpty() ? QStringLiteral("back-disabled.png") : QStringLiteral("back.png")));
        m_fwdBtn->setEnabled(!m_forward.isEmpty());
        m_fwdBtn->setIcon(icon(m_forward.isEmpty() ? QStringLiteral("forward-disabled.png") : QStringLiteral("forward.png")));
        m_backMenu->clear();
        static const QStringList names = {tr7("Adjust image settings with preview"), tr7("Manage 3D settings"),
                                          tr7("Set PhysX Configuration"), tr7("Device Settings"),
                                          tr7("View system information"), tr7("Profile policies")};
        for (int i = m_back.size() - 1; i >= 0; --i) {
            const int page = m_back.at(i);
            const int steps = m_back.size() - i;
            m_backMenu->addAction(names.value(page), this, [this, steps] {
                for (int s = 0; s < steps && !m_back.isEmpty(); ++s) {
                    m_forward.append(m_page);
                    m_page = m_back.takeLast();
                }
                go(m_page, false);
            });
        }
    }

    /* ---------- pages ---------- */
    QWidget *buildPage(int i)
    {
        switch (i) {
        case ImagePage:
            return buildImagePage();
        case Manage3DPage:
            return buildManagePage();
        case PhysXPage:
            return buildPhysXPage();
        case DevicePage:
            return buildDevicePage();
        case PoliciesPage:
            return buildPoliciesPage();
        case ResolutionPage:
            return m_disp.resolution();
        case DesktopColorPage:
            return m_disp.desktopColor();
        case RotatePage:
            return m_disp.rotate();
        case HdcpPage:
            return m_disp.hdcp();
        case AudioPage:
            return m_disp.digitalAudio();
        case SizePage:
            return m_disp.sizePosition();
        case MultiPage:
            return m_disp.multipleDisplays();
        case VideoColorPage:
            return m_disp.videoColor();
        case VideoImagePage:
            return m_disp.videoImage();
        default:
            return new QWidget;
        }
    }

    // --- Adjust image settings with preview ---
    QWidget *buildImagePage()
    {
        auto *page = new PageFrame(tr7("Adjust image settings with preview"),
                                   tr7("This page allows you to preview changes you make to image and rendering settings. These will be your default settings for 3D hardware-accelerated applications that use OpenGL."));
        page->banner->restore = [this] {
            m_state.imageMode = 0;
            m_state.imagePref = 2;
            setImagePreset();
            changed();
        };
        auto *panel = new RoundPanel;
        auto *g = new QGridLayout(panel);
        g->setContentsMargins(13, 12, 13, 14);
        g->setHorizontalSpacing(34);
        g->setVerticalSpacing(4);
        // left column: Performance / Quality thumbnails and "What do I look for?"
        auto *lc = new QVBoxLayout;
        lc->setSpacing(4);
        lc->addWidget(new QLabel(tr7("Performance:")));
        m_thumbPerf = new QLabel;
        lc->addWidget(m_thumbPerf);
        lc->addSpacing(8);
        lc->addWidget(new QLabel(tr7("Quality:")));
        m_thumbQual = new QLabel;
        lc->addWidget(m_thumbQual);
        lc->addSpacing(8);
        lc->addWidget(boldLabel(tr7("What do I look for?")));
        auto *look = new QLabel(tr7("The closer you get to the Performance side, the higher the frame rate, but curved lines will appear <a href=\"j\" style=\"color:#0066cc;\">jagged</a>.<br><br>The closer you get to the Quality side, the <a href=\"s\" style=\"color:#0066cc;\">smoother</a> curved lines will appear."));
        look->setWordWrap(true);
        look->setFixedWidth(112);
        connect(look, &QLabel::linkActivated, this, [page](const QString &l) {
            page->show(l == QLatin1String("j") ? tr7("Jaggedness (\"jaggies\") is the stair-step look on the edges of sloped and curved lines without antialiasing.")
                                               : tr7("Smoothness - line edges without steps thanks to antialiasing and texture filtering."),
                       {});
        });
        lc->addWidget(look);
        lc->addStretch(1);
        g->addLayout(lc, 0, 0, 2, 1);
        // right column: the preview, Pause, the three choices
        auto *rc = new QVBoxLayout;
        rc->setSpacing(4);
        rc->setContentsMargins(0, 0, 0, 0);
        rc->addWidget(new QLabel(tr7("Preview:")));
        rc->addSpacing(1);
        m_preview = new Preview;
        m_preview->setFixedSize(422, 202);
        rc->addWidget(m_preview);
        auto *pauseRow = new QHBoxLayout;
        pauseRow->addStretch(1);
        auto *pause = new QPushButton(tr7("Pause"));
        pause->setFixedSize(108, 23);
        pauseRow->addWidget(pause);
        rc->addLayout(pauseRow);
        connect(pause, &QPushButton::clicked, this, [this, pause] {
            m_preview->setPaused(!m_preview->paused());
            pause->setText(m_preview->paused() ? tr7("Play") : tr7("Pause"));
        });
        page->describe(pause, tr7("Stops or resumes the rotation in the preview."));
        m_imgGroup = new QButtonGroup(this);
        m_imgApp = new QRadioButton(tr7("Let the 3D application decide"));
        m_imgAdv = new QRadioButton(tr7("Use the advanced 3D image settings"));
        m_imgPref = new QRadioButton(tr7("Use my preference emphasizing:"));
        m_imgGroup->addButton(m_imgApp, 0);
        m_imgGroup->addButton(m_imgAdv, 1);
        m_imgGroup->addButton(m_imgPref, 2);
        page->describe(m_imgApp, tr7("The 3D application determines the image settings itself."),
                       tr7("Recommended if the game has its own antialiasing and filtering settings."));
        page->describe(m_imgAdv, tr7("Uses the settings on the Manage 3D settings page. To open it, click Take me there."));
        page->describe(m_imgPref, tr7("The slider sets the balance between performance and image quality."));
        rc->addSpacing(2);
        rc->setSpacing(1);
        // the choices stand 9 px in from the preview's edge
        m_imgApp->setContentsMargins(0, 0, 0, 0);
        auto *appRow = new QHBoxLayout;
        appRow->setContentsMargins(9, 0, 0, 0);
        appRow->addWidget(m_imgApp);
        appRow->addStretch(1);
        rc->addLayout(appRow);
        auto *advRow = new QHBoxLayout;
        advRow->setContentsMargins(9, 0, 0, 0);
        advRow->addWidget(m_imgAdv);
        advRow->addSpacing(18);
        advRow->addWidget(link(tr7("Take me there"), [this] {
            go(Manage3DPage);
        }));
        advRow->addStretch(1);
        rc->addLayout(advRow);
        // "Use my preference emphasizing:" sits on the top line of the frame round the slider
        auto *prefBox = new TitledBox;
        auto *pb = new QVBoxLayout(prefBox);
        pb->setContentsMargins(0, 0, 0, 10);
        pb->setSpacing(6);
        auto *prefRow = new QHBoxLayout;
        prefRow->setContentsMargins(9, 0, 0, 0);
        prefRow->addWidget(m_imgPref);
        m_prefName = boldLabel(QString());
        prefRow->addWidget(m_prefName);
        prefRow->addStretch(1);
        pb->addLayout(prefRow);
        auto *sh = new QHBoxLayout;
        sh->setContentsMargins(9, 2, 12, 4);
        m_perfLbl = new QLabel(tr7("Performance"));
        m_qualLbl = new QLabel(tr7("Quality"));
        m_slider = new Win7Slider;
        m_slider->setRange(0, 2);
        m_slider->setPageStep(1);
        // measured on Windows: 20 px from "Performance" to the track, 15 px from it to "Quality"
        sh->setSpacing(0);
        sh->addWidget(m_perfLbl);
        sh->addSpacing(14);
        sh->addWidget(m_slider, 1);
        sh->addSpacing(9);
        sh->addWidget(m_qualLbl);
        pb->addLayout(sh);
        prefBox->titleHeight = [this] {
            return m_imgPref->sizeHint().height();
        };
        prefBox->titleEnd = [this, prefBox] {
            return m_prefName->mapTo(prefBox, QPoint(m_prefName->sizeHint().width(), 0)).x();
        };
        rc->addWidget(prefBox);
        rc->addStretch(1);
        // the right column is as wide as the preview, like on Windows (Pause and the slider frame end with it)
        auto *rcw = new QWidget;
        rcw->setLayout(rc);
        rcw->setFixedWidth(422);
        g->addWidget(rcw, 0, 1, 2, 1, Qt::AlignTop);
        g->setColumnStretch(2, 1);
        page->body->addWidget(panel);
        connect(m_imgGroup, &QButtonGroup::idClicked, this, [this](int id) {
            m_state.imageMode = id;
            setImagePreset();
            changed();
        });
        connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
            if (m_refreshing) {
                return;
            }
            m_state.imagePref = v;
            setImagePreset();
            changed();
        });
        page->show(tr7("Uses the settings on the Manage 3D settings page. To open it, click Take me there."), {});
        return page;
    }

    /** The image page's choices written into the global 3D settings */
    void setImagePreset()
    {
        if (m_state.imageMode == 1) {
            return;
        }
        for (const QString &k : {QStringLiteral("aniso"), QStringLiteral("aamode"), QStringLiteral("aaset")}) {
            m_state.global.remove(k);
        }
        if (m_state.imageMode == 2) {
            static const int aniso[] = {1, 3, 4};   // Off, 4x, 16x
            static const int aaset[] = {0, 0, 3};   // -, -, 4x
            m_state.global.insert(QStringLiteral("aniso"), aniso[m_state.imagePref]);
            if (aaset[m_state.imagePref]) {
                m_state.global.insert(QStringLiteral("aamode"), 1);
                m_state.global.insert(QStringLiteral("aaset"), aaset[m_state.imagePref]);
            }
        }
    }

    // --- Manage 3D settings ---
    QWidget *buildManagePage()
    {
        auto *page = new PageFrame(tr7("Manage 3D settings"),
                                   tr7("You can change the global 3D settings and create overrides for specific programs. The overrides will be used automatically each time the specified programs are launched."));
        m_managePage = page;
        page->banner->restore = [this] {
            if (m_tabs->currentIndex() == 0) {
                m_state.global.clear();
                m_state.globalGpu = GpuAuto;
                m_state.power = 0;
                m_state.imageMode = 1;
            } else {
                const QString id = currentProgram();
                m_state.programs.remove(id);
                m_state.programGpu.remove(id);
            }
            changed();
        };
        auto *panel = new RoundPanel;
        auto *pv = new QVBoxLayout(panel);
        pv->setContentsMargins(20, 16, 20, 22);
        pv->setSpacing(10);
        pv->addWidget(boldLabel(tr7("I would like to use the following 3D settings:")));
        pv->addSpacing(14);
        m_tabs = new QTabWidget;
        m_tabs->setMaximumWidth(680);
        // Global Settings
        auto *gt = new QWidget;
        auto *gv = new QVBoxLayout(gt);
        gv->setContentsMargins(14, 12, 14, 12);
        gv->setSpacing(6);
        gv->addWidget(new QLabel(tr7("Preferred graphics processor:")));
        auto *gr = new QHBoxLayout;
        m_globalGpu = new QComboBox;
        m_globalGpu->addItem(QIcon(asset(QStringLiteral("restore.png"))), tr7("Auto-select"), GpuAuto);
        m_globalGpu->addItem(tr7("High-performance NVIDIA processor"), GpuNvidia);
        m_globalGpu->addItem(tr7("Integrated graphics"), GpuIntegrated);
        m_globalGpu->setMinimumWidth(230);
        gr->addWidget(m_globalGpu);
        gr->addSpacing(4);
        gr->addWidget(link(tr7("Manage updates..."), [this] {
            QMessageBox::information(this, tr7("Manage Updates"),
                                     tr7("NVIDIA program profiles are updated with the driver (currently version %1).")
                                         .arg(smi({QStringLiteral("driver_version")}).value(0)));
        }));
        gr->addStretch(1);
        gv->addLayout(gr);
        page->describe(m_globalGpu,
                       tr7("Specifies which graphics processor programs use. Auto-select uses integrated graphics, and NVIDIA for the programs it is selected for on the Program Settings tab."),
                       tr7("\"High-performance NVIDIA processor\" applies to games and Steam (Steam games inherit it). For other programs, choose the processor on the Program Settings tab."));
        connect(m_globalGpu, &QComboBox::activated, this, [this] {
            m_state.globalGpu = m_globalGpu->currentData().toInt();
            changed();
        });
        gv->addSpacing(6);
        gv->addWidget(new QLabel(tr7("Settings:")));
        m_globalTable = new SettingsTable;
        m_globalTable->setMinimumHeight(250);
        m_globalTable->value = [this](const QString &id) {
            if (id == QLatin1String("power")) {
                return m_state.power;
            }
            return m_state.global.value(id, feature(id)->def);
        };
        m_globalTable->changed = [this](const QString &id, int v) {
            if (id == QLatin1String("power")) {
                m_state.power = v;
            } else if (v == feature(id)->def) {
                m_state.global.remove(id);
            } else {
                m_state.global.insert(id, v);
            }
            if (id == QLatin1String("aniso") || id.startsWith(QLatin1String("aa"))) {
                m_state.imageMode = 1; // the image page now says "advanced"
            }
            changed();
        };
        m_globalTable->hovered = [page](const Feature &f) {
            page->show(f.description, f.usage);
        };
        gv->addWidget(m_globalTable, 1);
        auto *grr = new QHBoxLayout;
        grr->addStretch(1);
        auto *grest = restoreButton();
        grr->addWidget(grest);
        gv->addLayout(grr);
        connect(grest, &QPushButton::clicked, this, [this] {
            m_state.global.clear();
            m_state.power = 0;
            changed();
        });
        m_tabs->addTab(gt, tr7("Global Settings"));

        // Program Settings
        auto *pt = new QWidget;
        auto *ptv = new QVBoxLayout(pt);
        ptv->setContentsMargins(14, 12, 14, 12);
        ptv->setSpacing(6);
        ptv->addWidget(new QLabel(tr7("1. Select a program to customize:")));
        auto *pr = new QHBoxLayout;
        m_program = new QComboBox;
        m_program->setMinimumWidth(240);
        m_program->setMaxVisibleItems(16);
        pr->addWidget(m_program, 1);
        auto *add = new QPushButton(tr7("Add"));
        m_remove = new QPushButton(tr7("Remove"));
        auto *prest = restoreButton();
        for (QPushButton *b : {add, m_remove}) {
            b->setMinimumWidth(90);
        }
        pr->addWidget(add);
        pr->addWidget(m_remove);
        pr->addWidget(prest);
        ptv->addLayout(pr);
        m_onlyFound = new QCheckBox(tr7("Show only programs found on this computer"));
        m_onlyFound->setChecked(true);
        ptv->addWidget(m_onlyFound);
        ptv->addSpacing(8);
        ptv->addWidget(new QLabel(tr7("2. Select the preferred graphics processor for this program:")));
        m_progGpu = new QComboBox;
        m_progGpu->setMinimumWidth(300);
        auto *pgr = new QHBoxLayout;
        pgr->addWidget(m_progGpu);
        pgr->addStretch(1);
        ptv->addLayout(pgr);
        ptv->addSpacing(8);
        ptv->addWidget(new QLabel(tr7("3. Specify the settings for this program:")));
        m_progTable = new SettingsTable;
        m_progTable->perProgram = true;
        m_progTable->setMinimumHeight(200);
        m_progTable->value = [this](const QString &id) {
            return m_state.programs.value(currentProgram()).value(id, -1);
        };
        m_progTable->globalText = [this](const QString &id) {
            const Feature *f = feature(id);
            int g = id == QLatin1String("power") ? m_state.power : m_state.global.value(id, f->def);
            if (id == QLatin1String("aaset") && m_state.global.value(QStringLiteral("aamode"), 0) != 1) {
                g = 0;
            }
            return tr7("Use global setting (%1)").arg(f->choices.value(g).label);
        };
        m_progTable->changed = [this](const QString &id, int v) {
            const QString p = currentProgram();
            Values &vals = m_state.programs[p];
            if (v < 0) {
                vals.remove(id);
            } else {
                vals.insert(id, v);
            }
            if (vals.isEmpty()) {
                m_state.programs.remove(p);
            }
            m_state.exes.insert(p, programExe(p));
            changed();
        };
        m_progTable->hovered = [page](const Feature &f) {
            page->show(f.description, f.usage);
        };
        ptv->addWidget(m_progTable, 1);
        m_tabs->addTab(pt, tr7("Program Settings"));
        pv->addWidget(m_tabs);
        page->body->addWidget(panel);

        page->describe(m_program, tr7("Select the program whose settings you want to change. Its settings override the global ones each time it starts."),
                       tr7("To add a program that is not in the list, click Add."));
        page->describe(add, tr7("Adds a program to the list that does not have its own settings yet."));
        page->describe(m_remove, tr7("Removes the own settings of the selected program; it will use the global ones again."));
        page->describe(m_progGpu, tr7("The graphics processor the program runs with when started from the Start menu, the taskbar or the desktop."),
                       tr7("Applies to programs started after the changes are applied."));
        page->describe(m_onlyFound, tr7("If you clear this check box, the list also shows programs the NVIDIA driver has its own profiles for."));

        connect(m_program, &QComboBox::currentIndexChanged, this, [this] {
            if (!m_refreshing) {
                refreshProgram();
            }
        });
        connect(m_onlyFound, &QCheckBox::toggled, this, [this] {
            fillPrograms();
        });
        connect(m_progGpu, &QComboBox::activated, this, [this] {
            const QString p = currentProgram();
            const int g = m_progGpu->currentData().toInt();
            if (g == GpuGlobal) {
                m_state.programGpu.remove(p);
            } else {
                m_state.programGpu.insert(p, g);
                m_state.exes.insert(p, programExe(p));
            }
            changed();
        });
        connect(add, &QPushButton::clicked, this, [this] {
            addProgram();
        });
        connect(m_remove, &QPushButton::clicked, this, [this] {
            const QString p = currentProgram();
            m_state.programs.remove(p);
            m_state.programGpu.remove(p);
            changed();
        });
        connect(prest, &QPushButton::clicked, this, [this] {
            const QString p = currentProgram();
            m_state.programs.remove(p);
            m_state.programGpu.remove(p);
            changed();
        });
        page->show(tr7("Global settings apply to all programs that have no settings of their own."),
                   tr7("3D settings apply to OpenGL programs. Vulkan games and games through Proton (DXVK) use their own settings."));
        return page;
    }

    QString currentProgram() const
    {
        return m_program->currentData().toString();
    }
    QString programExe(const QString &id) const
    {
        for (const Program &p : m_programs) {
            if (p.id == id) {
                return p.exe;
            }
        }
        if (id.startsWith(QLatin1String("exe:"))) {
            return id.mid(4);
        }
        return m_state.exes.value(id);
    }

    void fillPrograms()
    {
        const QString keep = currentProgram();
        m_refreshing = true;
        m_program->clear();
        auto addItem = [this](const QString &id, const QString &name, const QString &exe, const QIcon &icon) {
            m_program->addItem(icon, QStringLiteral("%1 (%2)").arg(name, exe), id);
        };
        QStringList seen;
        // programs with settings first, like the Windows panel's list of profiles
        for (const Program &p : m_programs) {
            if (m_state.programs.contains(p.id) || m_state.programGpu.contains(p.id) || m_extra.contains(p.id)) {
                addItem(p.id, p.name, p.exe, QIcon::fromTheme(p.icon));
                seen << p.id;
            }
        }
        for (auto it = m_state.exes.cbegin(); it != m_state.exes.cend(); ++it) {
            if (!seen.contains(it.key()) && it.key().startsWith(QLatin1String("exe:"))) {
                addItem(it.key(), it.value(), it.value(), QIcon::fromTheme(QStringLiteral("application-x-executable")));
                seen << it.key();
            }
        }
        for (const Program &p : m_programs) {
            if (!seen.contains(p.id)) {
                addItem(p.id, p.name, p.exe, QIcon::fromTheme(p.icon));
            }
        }
        if (!m_onlyFound->isChecked()) {
            for (const QString &exe : driverProfilePrograms()) {
                if (!seen.contains(QStringLiteral("exe:") + exe)) {
                    addItem(QStringLiteral("exe:") + exe, exe, exe, QIcon::fromTheme(QStringLiteral("application-x-executable")));
                }
            }
        }
        const int at = m_program->findData(keep);
        m_program->setCurrentIndex(at >= 0 ? at : 0);
        m_refreshing = false;
        refreshProgram();
    }

    void refreshProgram()
    {
        const QString p = currentProgram();
        m_refreshing = true;
        m_progGpu->clear();
        static const QStringList names = {tr7("Auto-select"), tr7("High-performance NVIDIA processor"),
                                          tr7("Integrated graphics")};
        const bool desktopProgram = !p.startsWith(QLatin1String("exe:"));
        m_progGpu->addItem(tr7("Use global setting (%1)").arg(names.value(m_state.globalGpu)), GpuGlobal);
        m_progGpu->addItem(names.at(1), GpuNvidia);
        m_progGpu->addItem(names.at(2), GpuIntegrated);
        m_progGpu->setCurrentIndex(qMax(0, m_progGpu->findData(m_state.programGpu.value(p, GpuGlobal))));
        m_progGpu->setEnabled(desktopProgram);
        m_remove->setEnabled(m_state.programs.contains(p) || m_state.programGpu.contains(p));
        m_refreshing = false;
        m_progTable->reload();
    }

    void addProgram()
    {
        QDialog dlg(this);
        dlg.setWindowTitle(tr7("Select a Program"));
        auto *v = new QVBoxLayout(&dlg);
        v->addWidget(new QLabel(tr7("Select a program to customize:")));
        auto *list = new QTreeWidget;
        list->setHeaderLabels({tr7("Program"), tr7("Process")});
        list->setRootIsDecorated(false);
        for (const Program &p : m_programs) {
            auto *it = new QTreeWidgetItem(list, {p.name, p.exe});
            it->setIcon(0, QIcon::fromTheme(p.icon));
            it->setData(0, Qt::UserRole, p.id);
        }
        list->resizeColumnToContents(0);
        v->addWidget(list);
        auto *row = new QHBoxLayout;
        auto *browse = new QPushButton(tr7("Browse..."));
        row->addWidget(browse);
        row->addStretch(1);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        bb->button(QDialogButtonBox::Ok)->setText(tr7("Add Selected Program"));
        bb->button(QDialogButtonBox::Cancel)->setText(tr7("Cancel"));
        row->addWidget(bb);
        v->addLayout(row);
        QString chosen;
        connect(bb, &QDialogButtonBox::accepted, &dlg, [&] {
            if (list->currentItem()) {
                chosen = list->currentItem()->data(0, Qt::UserRole).toString();
                dlg.accept();
            }
        });
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        connect(list, &QTreeWidget::itemDoubleClicked, &dlg, [&](QTreeWidgetItem *it) {
            chosen = it->data(0, Qt::UserRole).toString();
            dlg.accept();
        });
        connect(browse, &QPushButton::clicked, &dlg, [&] {
            const QString f = QFileDialog::getOpenFileName(&dlg, tr7("Select a program"), QDir::homePath());
            if (!f.isEmpty()) {
                chosen = QStringLiteral("exe:") + QFileInfo(f).fileName();
                m_state.exes.insert(chosen, QFileInfo(f).fileName());
                dlg.accept();
            }
        });
        dlg.resize(480, 420);
        if (dlg.exec() != QDialog::Accepted || chosen.isEmpty()) {
            return;
        }
        m_extra << chosen;
        fillPrograms();
        m_program->setCurrentIndex(qMax(0, m_program->findData(chosen)));
    }

    // --- Set PhysX configuration ---
    QWidget *buildPhysXPage()
    {
        auto *page = new PageFrame(tr7("Set PhysX Configuration"),
                                   tr7("NVIDIA® PhysX® is a powerful physics engine which can utilize GPU acceleration to provide amazing real-time physics effects."));
        page->banner->restore = [this] {
            m_state.physx = 0;
            changed();
        };
        auto *panel = new RoundPanel;
        auto *v = new QVBoxLayout(panel);
        v->setContentsMargins(20, 16, 20, 22);
        v->setSpacing(10);
        v->addWidget(boldLabel(tr7("Select a PhysX processor:")));
        m_physx = new QComboBox;
        m_physx->addItems({tr7("Auto-select (recommended)"), gpuName(), tr7("CPU")});
        auto *r = new QHBoxLayout;
        r->setContentsMargins(16, 0, 0, 0);
        r->addWidget(m_physx);
        r->addStretch(1);
        v->addLayout(r);
        v->addSpacing(16);
        m_physxDiagram = new PhysXDiagram;
        auto *dr = new QHBoxLayout;
        dr->addSpacing(8);
        dr->addWidget(m_physxDiagram);
        dr->addStretch(1);
        v->addLayout(dr);
        page->body->addWidget(panel);
        page->describe(m_physx, tr7("Specifies which processor calculates PhysX physics in games."),
                       tr7("On Linux, games through Proton choose the PhysX processor themselves; this choice is only remembered."));
        connect(m_physx, &QComboBox::activated, this, [this](int i) {
            m_state.physx = i;
            changed();
        });
        page->show(tr7("The diagram shows the graphics processors of this computer and the display connected to the integrated graphics."), {});
        return page;
    }

    // --- Device settings: what the GPU is doing now ---
    QWidget *buildDevicePage()
    {
        auto *page = new PageFrame(tr7("Device Settings"),
                                   tr7("This page shows the current state of the graphics processor: clocks, temperature and load."));
        page->banner->setRestoreVisible(false);
        auto *panel = new RoundPanel;
        auto *g = new QGridLayout(panel);
        g->setContentsMargins(20, 16, 20, 22);
        g->setHorizontalSpacing(30);
        g->setVerticalSpacing(8);
        g->addWidget(boldLabel(gpuName()), 0, 0, 1, 2);
        const QStringList rows = {tr7("Graphics clock:"), tr7("Memory clock:"), tr7("GPU temperature:"),
                                  tr7("GPU utilization:"), tr7("Memory usage:"), tr7("Power draw:"),
                                  tr7("Performance state:")};
        for (int i = 0; i < rows.size(); ++i) {
            g->addWidget(new QLabel(rows.at(i)), i + 1, 0);
            auto *val = new QLabel(QStringLiteral("—"));
            g->addWidget(val, i + 1, 1);
            m_devValues << val;
        }
        g->setColumnStretch(2, 1);
        page->body->addWidget(panel);
        page->show(tr7("The values are updated every second while this page is open."),
                   tr7("Close this page to let the graphics processor sleep and save battery."));
        m_devTimer = new QTimer(this);
        m_devTimer->setInterval(1000);
        m_devTick = [this] {
            const QStringList v = smi({QStringLiteral("clocks.gr"), QStringLiteral("clocks.mem"), QStringLiteral("temperature.gpu"), QStringLiteral("utilization.gpu"),
                                       QStringLiteral("memory.used"), QStringLiteral("memory.total"), QStringLiteral("power.draw"), QStringLiteral("pstate")});
            auto or_ = [](const QString &s, const QString &unit) {
                return s.isEmpty() ? QStringLiteral("—") : s + unit;
            };
            m_devValues[0]->setText(or_(v[0], tr7(" MHz")));
            m_devValues[1]->setText(or_(v[1], tr7(" MHz")));
            m_devValues[2]->setText(or_(v[2], QStringLiteral(" °C")));
            m_devValues[3]->setText(or_(v[3], QStringLiteral(" %")));
            m_devValues[4]->setText(v[4].isEmpty() ? QStringLiteral("—") : tr7("%1 MB of %2 MB").arg(v[4], v[5]));
            m_devValues[5]->setText(or_(v[6], tr7(" W")));
            m_devValues[6]->setText(or_(v[7], QString()));
        };
        connect(m_devTimer, &QTimer::timeout, this, [this] {
            m_devTick();
        });
        connect(m_stack, &QStackedWidget::currentChanged, this, [this](int i) {
            if (i == DevicePage) {
                m_devTimer->start();
                m_devTick();
            } else {
                m_devTimer->stop();
            }
        });
        return page;
    }

    // --- Profile policies: which programs have their own settings ---
    QWidget *buildPoliciesPage()
    {
        auto *page = new PageFrame(tr7("Profile policies"),
                                   tr7("Program profiles are applied automatically when programs start. All programs with their own settings are shown here."));
        page->banner->setRestoreVisible(false);
        auto *panel = new RoundPanel;
        auto *v = new QVBoxLayout(panel);
        v->setContentsMargins(20, 16, 20, 22);
        v->addWidget(boldLabel(tr7("Programs with their own settings:")));
        m_policies = new QTreeWidget;
        m_policies->setHeaderLabels({tr7("Program"), tr7("Graphics processor"), tr7("Changed settings")});
        m_policies->setRootIsDecorated(false);
        m_policies->setMinimumHeight(240);
        v->addWidget(m_policies);
        page->body->addWidget(panel);
        page->show(tr7("To change the settings of a program, open Manage 3D settings → Program Settings."), {});
        connect(m_policies, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
            go(Manage3DPage);
            m_tabs->setCurrentIndex(1);
            m_program->setCurrentIndex(qMax(0, m_program->findData(it->data(0, Qt::UserRole))));
        });
        return page;
    }

    void fillPolicies()
    {
        m_policies->clear();
        QStringList ids = m_state.programs.keys() + m_state.programGpu.keys();
        ids.removeDuplicates();
        static const QStringList gpus = {tr7("Auto-select"), tr7("High-performance NVIDIA processor"), tr7("Integrated graphics")};
        for (const QString &id : ids) {
            QString name = id.startsWith(QLatin1String("exe:")) ? id.mid(4) : id;
            for (const Program &p : m_programs) {
                if (p.id == id) {
                    name = p.name;
                }
            }
            QStringList changedNames;
            for (auto v = m_state.programs.value(id).cbegin(); v != m_state.programs.value(id).cend(); ++v) {
                if (v.value() >= 0) {
                    changedNames << feature(v.key())->label;
                }
            }
            const int g = m_state.programGpu.value(id, GpuGlobal);
            auto *it = new QTreeWidgetItem(m_policies, {name, g == GpuGlobal ? tr7("Global setting") : gpus.value(g),
                                                        changedNames.isEmpty() ? QStringLiteral("—") : changedNames.join(QStringLiteral(", "))});
            it->setData(0, Qt::UserRole, id);
        }
        for (int c = 0; c < 2; ++c) {
            m_policies->resizeColumnToContents(c);
        }
    }

    /* ---------- System Information ---------- */
    void systemInformation()
    {
        QDialog dlg(this);
        dlg.setWindowTitle(tr7("System Information"));
        dlg.setWindowFlags(dlg.windowFlags() & ~Qt::WindowContextHelpButtonHint);
        auto *v = new QVBoxLayout(&dlg);
        auto *tabs = new QTabWidget;
        // Display
        auto *disp = new QWidget;
        auto *dv = new QVBoxLayout(disp);
        dv->addWidget(boldLabel(tr7("System Information")));
        auto *top = new QGridLayout;
        top->setContentsMargins(12, 2, 0, 6);
        QString os = QStringLiteral("Linux");
        {
            QSettings rel(QStringLiteral("/etc/os-release"), QSettings::IniFormat);
            os = rel.value(QStringLiteral("PRETTY_NAME"), os).toString() + tr7(", kernel ") + run(QStringLiteral("uname"), {QStringLiteral("-r")});
        }
        const QString vk = run(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("vulkaninfo --summary 2>/dev/null | grep -m1 'Vulkan Instance Version' | sed 's/.*: //'")});
        top->addWidget(new QLabel(tr7("Operating system:")), 0, 0);
        top->addWidget(new QLabel(os), 0, 1);
        top->addWidget(new QLabel(tr7("Vulkan version:")), 1, 0);
        top->addWidget(new QLabel(vk.isEmpty() ? QStringLiteral("—") : vk), 1, 1);
        top->setColumnStretch(1, 1);
        dv->addLayout(top);
        dv->addWidget(new QLabel(tr7("Graphics card information")));
        auto *split = new QHBoxLayout;
        auto *items = new QTreeWidget;
        items->setHeaderLabels({tr7("Items")});
        items->setRootIsDecorated(false);
        auto *gpuItem = new QTreeWidgetItem(items, {gpuName()});
        gpuItem->setIcon(0, QIcon(asset(QStringLiteral("restore.png"))));
        items->setCurrentItem(gpuItem);
        items->setFixedWidth(200);
        split->addWidget(items);
        auto *details = new QTreeWidget;
        details->setHeaderLabels({tr7("Details")});
        details->setRootIsDecorated(false);
        details->setColumnCount(2);
        details->setHeaderLabels({tr7("Details"), QString()});
        const QStringList s = smi({QStringLiteral("driver_version"), QStringLiteral("clocks.max.gr"), QStringLiteral("clocks.max.mem"),
                                   QStringLiteral("memory.total"), QStringLiteral("vbios_version"), QStringLiteral("pcie.link.gen.max"),
                                   QStringLiteral("pcie.link.width.max"), QStringLiteral("pci.bus_id")});
        const QString cores = gpuAttribute(QStringLiteral("CUDACores"));
        const QString bus = gpuAttribute(QStringLiteral("GPUMemoryInterface"));
        const QString irq = gpuAttribute(QStringLiteral("Irq"));
        QString rate, bandwidth;
        if (!s[2].isEmpty() && !bus.isEmpty()) {
            const double mts = s[2].toDouble() * 2; // GDDR6: two transfers per memory clock reported by nvidia-smi
            rate = tr7("%1 MHz").arg(qRound(mts));
            bandwidth = tr7("%1 GB/s").arg(QString::number(mts * bus.toDouble() / 8 / 1000, 'f', 2));
        }
        const QList<QPair<QString, QString>> rows = {
            {tr7("Driver version:"), s[0]},
            {tr7("CUDA Cores:"), cores},
            {tr7("Graphics clock:", "details"), s[1].isEmpty() ? QString() : s[1] + tr7(" MHz")},
            {tr7("Memory data rate:"), rate},
            {tr7("Memory interface:"), bus.isEmpty() ? QString() : bus + tr7("-bit")},
            {tr7("Memory bandwidth:"), bandwidth},
            {tr7("Dedicated video memory:"), s[3].isEmpty() ? QString() : s[3] + tr7(" MB GDDR6")},
            {tr7("Video BIOS version:"), s[4]},
            {QStringLiteral("IRQ:"), irq},
            {tr7("Bus:"), s[5].isEmpty() ? QString() : QStringLiteral("PCI Express %1.0 x%2").arg(s[5], s[6])},
            {tr7("PCI location:"), s[7]},
        };
        for (const auto &r : rows) {
            new QTreeWidgetItem(details, {r.first, r.second.isEmpty() ? QStringLiteral("—") : r.second});
        }
        details->resizeColumnToContents(0);
        split->addWidget(details, 1);
        dv->addLayout(split, 1);
        tabs->addTab(disp, tr7("Display", "system info"));
        // Components
        auto *comp = new QWidget;
        auto *cv = new QVBoxLayout(comp);
        auto *files = new QTreeWidget;
        files->setHeaderLabels({tr7("File"), tr7("Version"), tr7("Description")});
        files->setRootIsDecorated(false);
        const QString ver = s[0];
        const QList<QStringList> comps = {
            {QStringLiteral("nvidia.ko"), run(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("modinfo -F version nvidia 2>/dev/null")}),
             tr7("NVIDIA kernel module")},
            {QStringLiteral("libGLX_nvidia.so.0"), ver, tr7("NVIDIA OpenGL driver")},
            {QStringLiteral("libnvidia-glcore.so"), ver, tr7("NVIDIA OpenGL core")},
            {QStringLiteral("nvidia_icd.json"), ver, tr7("NVIDIA Vulkan driver")},
            {QStringLiteral("libcuda.so.1"), ver, tr7("NVIDIA CUDA driver")},
            {QStringLiteral("libnvcuvid.so.1"), ver, tr7("NVIDIA video decode")},
            {QStringLiteral("libnvidia-encode.so.1"), ver, tr7("NVIDIA video encode (NVENC)")},
            {QStringLiteral("nvidia-settings"), run(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("nvidia-settings --version 2>/dev/null | grep -o '[0-9.]\\+' | head -1")}),
             tr7("NVIDIA X server settings")},
        };
        for (const QStringList &c : comps) {
            new QTreeWidgetItem(files, c);
        }
        for (int c = 0; c < 3; ++c) {
            files->resizeColumnToContents(c);
        }
        cv->addWidget(files);
        tabs->addTab(comp, tr7("Components"));
        v->addWidget(tabs, 1);
        auto *row = new QHBoxLayout;
        auto *about = new QPushButton(tr7("About"));
        auto *save = new QPushButton(tr7("Save"));
        auto *close = new QPushButton(tr7("Close"));
        close->setDefault(true);
        row->addWidget(about);
        row->addStretch(1);
        row->addWidget(save);
        row->addWidget(close);
        v->addLayout(row);
        connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
        connect(about, &QPushButton::clicked, &dlg, [&dlg, s] {
            QMessageBox::about(&dlg, tr7("About NVIDIA Control Panel"), tr7("<b>NVIDIA Control Panel</b><br>Driver version: %1").arg(s[0]));
        });
        connect(save, &QPushButton::clicked, &dlg, [&dlg, os, rows, comps] {
            const QString f = QFileDialog::getSaveFileName(&dlg, tr7("Save System Information"),
                                                           QDir::homePath() + tr7("/NVIDIA System Information.txt"), tr7("Text files (*.txt)"));
            if (f.isEmpty()) {
                return;
            }
            QFile out(f);
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                QString t = tr7("Operating system: %1\n\n%2\n").arg(os, gpuName());
                for (const auto &r : rows) {
                    t += QStringLiteral("  %1 %2\n").arg(r.first, r.second);
                }
                t += tr7("\nComponents\n");
                for (const QStringList &c : comps) {
                    t += QStringLiteral("  %1  %2  %3\n").arg(c.value(0), c.value(1), c.value(2));
                }
                out.write(t.toUtf8());
            }
        });
        dlg.resize(600, 470);
        dlg.exec();
    }

    /* ---------- state ---------- */
    void changed()
    {
        refresh();
    }

    void refresh()
    {
        m_refreshing = true;
        const bool dirty = !(m_state == m_saved) || m_disp.dirty();
        m_applyBar->setVisible(dirty);
        m_applyAct->setEnabled(dirty);
        m_undo->setEnabled(dirty);
        m_runMenu->setChecked(m_state.runMenu);
        m_osd->setChecked(m_state.osd);
        m_globalGpu->setCurrentIndex(qMax(0, m_globalGpu->findData(m_state.globalGpu)));
        m_globalTable->reload();
        m_refreshing = false;
        fillPrograms();
        m_refreshing = true;
        // image page
        m_imgGroup->button(m_state.imageMode)->setChecked(true);
        m_slider->setValue(m_state.imagePref);
        static const QStringList prefNames = {tr7("Performance"), tr7("Balanced"), tr7("Quality")};
        m_prefName->setText(prefNames.value(m_state.imagePref));
        const bool pref = m_state.imageMode == 2;
        m_slider->setEnabled(pref);
        m_perfLbl->setEnabled(pref);
        m_qualLbl->setEnabled(pref);
        m_preview->quality = m_state.imageMode == 2 ? m_state.imagePref : (m_state.global.value(QStringLiteral("aamode"), 0) == 1 ? 2 : 1);
        m_preview->update();
        if (m_thumbPerf->pixmap().isNull()) {
            m_thumbPerf->setPixmap(QPixmap::fromImage(thumb(0)));
            m_thumbQual->setPixmap(QPixmap::fromImage(thumb(2)));
        }
        // PhysX
        m_physx->setCurrentIndex(m_state.physx);
        m_physxDiagram->choice = m_state.physx;
        m_physxDiagram->update();
        fillPolicies();
        m_refreshing = false;
    }

    QImage thumb(int q) const
    {
        // NVIDIA's own pictures (nvGameS.dll, PNG 1003 / 1004)
        return QImage(asset(q == 0 ? QStringLiteral("thumb-performance.png") : QStringLiteral("thumb-quality.png")));
    }

    void applyChanges()
    {
        if (m_disp.dirty()) {
            m_disp.apply(this);
        }
        const QString err = applyState(m_state, m_saved, m_programs);
        if (!err.isEmpty()) {
            QMessageBox::warning(this, tr7("NVIDIA Control Panel"), err);
        }
        m_programs = installedPrograms();
        m_saved = loadState(m_programs);
        // keep the choices the system does not report back
        m_saved.imageMode = m_state.imageMode;
        m_saved.imagePref = m_state.imagePref;
        m_state = m_saved;
        refresh();
    }

    NvDisp::Pages m_disp;
    QList<Program> m_programs;
    QStringList m_extra; // programs added with "Add" that have no settings yet
    State m_saved, m_state;
    bool m_refreshing = false;
    int m_page = Manage3DPage;
    QList<int> m_back, m_forward;
    TaskTree *m_tree = nullptr;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_applyBar = nullptr;
    QAction *m_undo = nullptr, *m_applyAct = nullptr, *m_runMenu = nullptr, *m_osd = nullptr;
    QToolButton *m_backBtn = nullptr, *m_fwdBtn = nullptr, *m_backArrow = nullptr;
    QMenu *m_backMenu = nullptr;
    PageFrame *m_managePage = nullptr;
    QTabWidget *m_tabs = nullptr;
    QComboBox *m_globalGpu = nullptr, *m_program = nullptr, *m_progGpu = nullptr, *m_physx = nullptr;
    SettingsTable *m_globalTable = nullptr, *m_progTable = nullptr;
    QPushButton *m_remove = nullptr;
    QCheckBox *m_onlyFound = nullptr;
    QButtonGroup *m_imgGroup = nullptr;
    QRadioButton *m_imgApp = nullptr, *m_imgAdv = nullptr, *m_imgPref = nullptr;
    QLabel *m_prefName = nullptr, *m_perfLbl = nullptr, *m_qualLbl = nullptr, *m_thumbPerf = nullptr, *m_thumbQual = nullptr;
    QSlider *m_slider = nullptr;
    Preview *m_preview = nullptr;
    PhysXDiagram *m_physxDiagram = nullptr;
    QList<QLabel *> m_devValues;
    QTimer *m_devTimer = nullptr;
    std::function<void()> m_devTick;
    QTreeWidget *m_policies = nullptr;
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Win7I18n::install(&app);
    app.setApplicationName(QStringLiteral("seven-nvidia"));
    app.setApplicationDisplayName(QString());
    app.setDesktopFileName(QStringLiteral("seven-nvidia"));
    if (app.arguments().contains(QStringLiteral("--apply-display"))) {
        // at login: desktop colour, digital vibrance / hue and scaling of each display
        Nv::NvDisp::applyAll();
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--apply-power"))) {
        // at login: the power management mode chosen in the panel
        QSettings &st = Nv::store();
        const int mode = st.value(QStringLiteral("PowerMizerMode"), -1).toInt();
        if (mode >= 0) {
            Nv::setPowerMizerMode(mode);
        }
        return 0;
    }
    Nv::Window w;
    const int pageArg = app.arguments().indexOf(QStringLiteral("--page"));
    if (pageArg > 0) {
        w.go(app.arguments().value(pageArg + 1).toInt());
    }
    // for checking the look without a window on the screen: --screenshot <file> (with QT_QPA_PLATFORM=offscreen)
    const int shotArg = app.arguments().indexOf(QStringLiteral("--screenshot"));
    if (shotArg > 0) {
        // outside the screen and unmanaged, so nothing shows up; grab() paints the window itself
        w.setWindowFlag(Qt::X11BypassWindowManagerHint);
        w.setGeometry(-4000, -4000, 1060, 740);
        w.show();
        QTimer::singleShot(1500, &w, [&w, &app, shotArg] {
            w.grab().save(app.arguments().value(shotArg + 1));
            app.quit();
        });
        return app.exec();
    }
    w.show();
    return app.exec();
}
