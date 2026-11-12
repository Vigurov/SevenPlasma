#define WIN7_WINDOWCLASS_XCB // (before any header includes win7windowclass.h)
#include "win7i18n.h"
/*
 * Control Panel in the style of Windows 7: "Category", "Large icons" and "Small icons" views.
 * Every link opens the matching KDE settings module or one of our Windows 7 style tools.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "win7chrome.h"
#include "win7power.h"
#include "win7network.h"
#include "win7display.h"
#include "win7displayswitch.h"
#include "win7background.h"
#include "win7openwith.h"
#include "win7portal.h"
#include "win7windowcolor.h"
#include "win7screensaver.h"
#include "win7firewall.h"
#include "win7trayicons.h"
#include "win7taskbar.h"
#include "win7region.h"
#include "win7sound.h"
#include "win7sndvol.h"
#include "win7sysprops.h"
#include "win7visualfx.h"
#include "win7devices.h"
#include "win7mouse.h"
#include "win7autoplay.h"
#include "win7programs.h"
#include "win7users.h"
#include "win7access.h"
#include "win7cleanup.h"
#include "win7memtest.h"
#include "win7diskmgmt.h"
#include "win7eventvwr.h"
#include "win7backup.h"
#include "win7wf.h"
#include "win7fonts.h"
#include "win7fontview.h"
#include "win7charmap.h"
#include "win7cttune.h"
#include "win7folderopts.h"
#include "win7compmgmt.h"
#include "win7update.h"
#include "win7actioncenter.h"
#include "win7sharing.h"
#include "win7trash.h"
#include "win7perf.h"
#include "win7games.h"
#include "win7indexing.h"
#include "win7properties.h"
#include "win7notepad.h"
#include "win7run.h"
#include "win7inetopts.h"
#include "win7colormgmt.h"

#include <AeroQt/insetwindow.h>
#include <AeroQt/stylesheet.h>

#include <QActionGroup>
#include <QListWidget>
#include <QCollator>
#include <QScrollArea>
#include <QDir>
#include <QProcess>
#include <QTimer>

namespace
{
/* ---------------- what the links open ---------------- */

std::function<void(int)> g_openCategoryPage; // set by the window; index into categoryPages()
std::function<void(int)> g_openView; // set by the window; any view, e.g. Win7Power::PlansView
int g_launchView = -1; // the page asked for on the command line (its OK / Cancel closes the window)

std::function<void()> view(int v)
{
    return [v] {
        g_openView(v);
    };
}


/** Windows 7 buttons have no icons, except the UAC shield. Kvantum's iconless_pushbutton (set for every
 *  program) removes all of them, so seven-control draws the icons it sets itself (shields) on top of it. */
class Win7ButtonStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;
    int styleHint(StyleHint h, const QStyleOption *o, const QWidget *w, QStyleHintReturn *r) const override
    {
        if (h == SH_DialogButtonBox_ButtonsHaveIcons) {
            return 0; // no ticks and crosses on OK / Cancel
        }
        return QProxyStyle::styleHint(h, o, w, r);
    }
    QSize sizeFromContents(ContentsType t, const QStyleOption *o, const QSize &s, const QWidget *w) const override
    {
        QSize sz = QProxyStyle::sizeFromContents(t, o, s, w);
        if (t == CT_PushButton) {
            if (auto *b = qstyleoption_cast<const QStyleOptionButton *>(o); b && !b->icon.isNull() && !b->text.isEmpty()) {
                sz.rwidth() += b->iconSize.width() + 8;
            }
        }
        return sz;
    }
    void drawControl(ControlElement el, const QStyleOption *o, QPainter *p, const QWidget *w) const override
    {
        if (el == CE_PushButton) {
            // Kvantum paints the whole button here: bevel without text, then our label
            if (auto *b = qstyleoption_cast<const QStyleOptionButton *>(o); b && !b->icon.isNull() && !b->text.isEmpty()) {
                QStyleOptionButton bevel(*b);
                bevel.text.clear();
                bevel.icon = QIcon();
                QProxyStyle::drawControl(CE_PushButton, &bevel, p, w);
                drawControl(CE_PushButtonLabel, b, p, w);
                return;
            }
        }
        if (el == CE_PushButtonLabel) {
            if (auto *b = qstyleoption_cast<const QStyleOptionButton *>(o); b && !b->icon.isNull() && !b->text.isEmpty()) {
                const QSize is = b->iconSize.isValid() ? b->iconSize : QSize(16, 16);
                const QFontMetrics fm(b->fontMetrics);
                const int tw = fm.horizontalAdvance(b->text), gap = 4;
                const int total = is.width() + gap + tw;
                int x = b->rect.center().x() - total / 2 + 1;
                const int y = b->rect.center().y() - is.height() / 2 + 1;
                const bool on = b->state & State_Enabled;
                p->drawPixmap(x, y, b->icon.pixmap(is, on ? QIcon::Normal : QIcon::Disabled));
                QStyleOptionButton t(*b);
                t.icon = QIcon();
                // Kvantum centres the text with its own side margins: give it room on both sides
                const int pad = 12;
                t.rect = QRect(x + is.width() + gap - pad, b->rect.top(), tw + 2 * pad, b->rect.height());
                QProxyStyle::drawControl(el, &t, p, w);
                return;
            }
        }
        QProxyStyle::drawControl(el, o, p, w);
    }
};

/** Windows 7 dialogs run as their own program, so the title bar shows their own icon */
std::function<void()> self(const QStringList &args)
{
    return [args] {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
    };
}

std::function<void()> editActivePlan()
{
    return [] {
        g_openView(Win7Power::EditView + Win7Power::planIndex(Win7Power::activePlan()));
    };
}

std::function<void()> kcm(const QString &module)
{
    return [module] {
        launch(QStringLiteral("/usr/bin/kcmshell6"), {module});
    };
}

std::function<void()> tool(const QString &name, const QString &fallback = QStringLiteral("systemsettings"))
{
    return [name, fallback] {
        launchWin7Tool(name, fallback);
    };
}

std::function<void()> program(const QString &exe, const QStringList &args = {})
{
    return [exe, args] {
        launch(exe, args);
    };
}

/** "Administrative Tools" is a folder of shortcuts in Explorer, like in Windows 7 */
std::function<void()> adminTools()
{
    return [] {
        // the folder is (re)made each time: its name and the shortcuts' names in the UI language
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/seven-control/")
            + tr7("Administrative Tools");
        QDir().mkpath(dir);
        const QString self = QCoreApplication::applicationFilePath();
        struct Tool {
            const char *name, *comment;
            QString exec;
            const char *icon;
        };
        const Tool tools[] = {
            {"Windows Firewall with Advanced Security", "Configure firewall rules", self + QStringLiteral(" --wf"), "seven-at-firewall"},
            {"Windows Memory Diagnostic", "Check your computer for memory problems", self + QStringLiteral(" --memtest"), "seven-at-memtest"},
            {"Disk Management", "Create, delete and format disk partitions", self + QStringLiteral(" --diskmgmt"), "seven-at-disks"},
            {"Computer Management", "Hardware and system information", self + QStringLiteral(" --compmgmt"), "seven-at-compmgmt"},
            {"System Configuration", "Programs that start when you log on", QStringLiteral("/usr/bin/kcmshell6 kcm_autostart"), "seven-at-msconfig"},
            {"Event Viewer", "View system and program logs", self + QStringLiteral(" --eventvwr"), "seven-at-events"},
            {"Performance Monitor", "Monitor the performance of your computer", QStringLiteral("plasma-systemmonitor"), "seven-at-perfmon"},
        };
        for (const Tool &t : tools) {
            QFile f(dir + QLatin1Char('/') + tr7(t.name).replace(QLatin1Char('/'), QLatin1Char('-')) + QStringLiteral(".desktop"));
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=%1\nComment=%2\nExec=%3\nIcon=%4\n")
                            .arg(tr7(t.name), tr7(t.comment), t.exec, QString::fromLatin1(t.icon))
                            .toUtf8());
                f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
            }
        }
        QFile d(dir + QStringLiteral("/.directory"));
        if (d.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            d.write("[Desktop Entry]\nIcon=seven-at-folder\n");
        }
        launch(QStringLiteral("dolphin"), {dir});
    };
}

struct SubLink {
    QString text;
    std::function<void()> open;
    bool shield = false;
};

struct CategoryInfo {
    QString icon;
    QString title;
    std::function<void()> open;
    QList<SubLink> links;
};

const QList<CategoryInfo> &leftColumn()
{
    static const QList<CategoryInfo> list{
        {QStringLiteral("seven-cp-system"),
         tr7("System and Security"),
         [] {
             g_openCategoryPage(1);
         },
         {{tr7("Review your computer's status"), view(Win7Action::CenterView)},
          {tr7("Back up your computer"), view(Win7Backup::BackupView)},
          // no troubleshooting packs here: the Action Center has the problem reports and fixes
          {tr7("Find and fix problems"), view(Win7Action::CenterView)}}},
        {QStringLiteral("seven-cp-network"),
         tr7("Network and Internet"),
         [] {
             g_openCategoryPage(2);
         },
         {{tr7("View network status and tasks"), view(Win7Net::CenterView)},
          {tr7("Choose homegroup and sharing options"), program(QStringLiteral("dolphin"), {QStringLiteral("smb:/")})}}},
        {QStringLiteral("seven-cp-hardware"),
         tr7("Hardware and Sound"),
         [] {
             g_openCategoryPage(4);
         },
         {{tr7("View devices and printers"), view(Win7Dev::DevicesView)},
          {tr7("Add a device"), [] { Win7Dev::addDeviceWizard(nullptr, [] {}); }}}},
        {QStringLiteral("seven-cp-programs"),
         tr7("Programs"),
         [] {
             g_openCategoryPage(5);
         },
         {{tr7("Uninstall a program"), view(Win7Programs::ProgramsView)}}},
    };
    return list;
}

const QList<CategoryInfo> &rightColumn()
{
    static const QList<CategoryInfo> list{
        {QStringLiteral("seven-cp-users"),
         tr7("User Accounts and Family Safety"),
         [] {
             g_openCategoryPage(6);
         },
         {{tr7("Add or remove user accounts"), view(Win7Users::ManageView), true},
          // Linux has no parental controls of its own: the accounts page, where they would be set per user
          {tr7("Set up parental controls for any user"), view(Win7Users::ManageView), true}}},
        {QStringLiteral("seven-cp-appearance"),
         tr7("Appearance and Personalization"),
         [] {
             g_openCategoryPage(0);
         },
         {{tr7("Change the theme"), view(Win7Display::PersonalizeView)},
          {tr7("Change desktop background"), view(Win7Background::BackgroundView)},
          {tr7("Adjust screen resolution"), view(Win7Display::ResolutionView)}}},
        {QStringLiteral("seven-cp-clock"),
         tr7("Clock, Language, and Region"),
         [] {
             g_openCategoryPage(3);
         },
         {{tr7("Change keyboards or other input methods"), self({QStringLiteral("--text-services")})},
          {tr7("Change display language"), self({QStringLiteral("--region"), QStringLiteral("1")})}}},
        {QStringLiteral("seven-cp-access"),
         tr7("Ease of Access"),
         [] {
             g_openCategoryPage(7);
         },
         {{tr7("Let Windows suggest settings"), view(Win7Access::QuestionsView)},
          {tr7("Optimize visual display"), view(Win7Access::SeeView)}}},
    };
    return list;
}

/* ---------------- "All Control Panel Items" ---------------- */

struct Item {
    QString name;
    QString icon;      // Windows 7 16 px icon (seven-cpi-*)
    QString bigIcon;   // theme icon for the 32 px view
    std::function<void()> open;
};

/* a GPU of this PCI vendor (0x1002 AMD, 0x8086 Intel, 0x10de NVIDIA) in /sys/class/drm */
bool hasGpuVendor(const QString &vendor)
{
    const QDir d(QStringLiteral("/sys/class/drm"));
    for (const QString &c : d.entryList({QStringLiteral("card?"), QStringLiteral("card??")}, QDir::AllEntries | QDir::System)) {
        QFile f(d.absoluteFilePath(c) + QStringLiteral("/device/vendor"));
        if (f.open(QIODevice::ReadOnly) && QString::fromLatin1(f.readAll()).trimmed() == vendor) {
            return true;
        }
    }
    return false;
}

/** the vendor panels to show, like the desktop menu: NVIDIA with an NVIDIA GPU on NVIDIA's driver, Catalyst
 *  with an AMD GPU, Intel's with an Intel GPU (~/.config/seven-gpu-panels "nvidia amd intel" adds them for tests) */
struct GpuPanels {
    bool nvidia, amd, intel;
};
GpuPanels gpuPanels()
{
    QFile o(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/seven-gpu-panels"));
    const QString force = o.open(QIODevice::ReadOnly) ? QString::fromLatin1(o.readAll()) : QString();
    return {(hasGpuVendor(QStringLiteral("0x10de")) && QFileInfo::exists(QStringLiteral("/sys/module/nvidia"))) || force.contains(QLatin1String("nvidia")),
            hasGpuVendor(QStringLiteral("0x1002")) || force.contains(QLatin1String("amd")),
            hasGpuVendor(QStringLiteral("0x8086")) || force.contains(QLatin1String("intel"))};
}

bool hiddenVendorPanel(const QString &icon, const GpuPanels &g)
{
    return (icon == QLatin1String("seven-nvidia") && !g.nvidia) || (icon == QLatin1String("seven-ccc") && !g.amd)
        || (icon == QLatin1String("seven-igfx") && !g.intel);
}

QList<Item> allItems()
{
    QList<Item> items{
        {tr7("AutoPlay"), QStringLiteral("seven-cpi-autostart"), QStringLiteral("seven-cpi-autostart"), view(Win7Autoplay::AutoplayView)},
        {tr7("Recovery"), QStringLiteral("seven-cpi-recovery"), QStringLiteral("seven-cpi-recovery"), view(Win7Backup::RecoveryView)},
        {tr7("Device Manager"), QStringLiteral("seven-cpi-devmgr"), QStringLiteral("seven-cpi-devmgr"), tool(QStringLiteral("seven-devmgr"), QStringLiteral("kinfocenter"))},
        {tr7("Sound"), QStringLiteral("seven-cpi-sound"), QStringLiteral("seven-cpi-sound"), self({QStringLiteral("--sound")})},
        {tr7("Taskbar and Start Menu"), QStringLiteral("seven-cpi-taskbar"), QStringLiteral("seven-cpi-taskbar"), [] { QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--taskbar-props")}); }},
        {tr7("Devices and Printers"), QStringLiteral("seven-cpi-printers"), QStringLiteral("seven-cpi-printers"), view(Win7Dev::DevicesView)},
        {tr7("Display"), QStringLiteral("seven-cpi-display"), QStringLiteral("seven-cpi-display"), view(Win7Display::DisplayView)},
        {tr7("Administrative Tools"), QStringLiteral("seven-cpi-admin"), QStringLiteral("seven-cpi-admin"), adminTools()},
        {tr7("Notification Area Icons"), QStringLiteral("seven-cpi-trayicons"), QStringLiteral("seven-cpi-trayicons"), view(Win7Tray::IconsView)},
        {tr7("Indexing Options"), QStringLiteral("seven-cpi-indexing"), QStringLiteral("seven-cpi-indexing"), self({QStringLiteral("--indexing")})},
        {tr7("User Accounts"), QStringLiteral("seven-cpi-users"), QStringLiteral("seven-cpi-users"), view(Win7Users::HomeView)},
        {tr7("Ease of Access Center"), QStringLiteral("seven-cpi-access"), QStringLiteral("seven-cpi-access"), view(Win7Access::CenterView)},
        {tr7("Power Options"), QStringLiteral("seven-cpi-power"), QStringLiteral("seven-cpi-power"), view(Win7Power::PlansView)},
        {tr7("Backup and Restore"), QStringLiteral("seven-cpi-backup"), QStringLiteral("seven-cpi-backup"), view(Win7Backup::BackupView)},
        {tr7("Date and Time"), QStringLiteral("seven-cpi-datetime"), QStringLiteral("seven-cpi-datetime"), self({QStringLiteral("--datetime")})},
        {tr7("HomeGroup"), QStringLiteral("seven-cpi-homegroup"), QStringLiteral("seven-cpi-homegroup"), program(QStringLiteral("dolphin"), {QStringLiteral("smb:/")})},
        {tr7("Keyboard"), QStringLiteral("seven-cpi-keyboard"), QStringLiteral("seven-cpi-keyboard"), self({QStringLiteral("--keyboard")})},
        {tr7("Programs and Features"), QStringLiteral("seven-cpi-programs"), QStringLiteral("seven-cpi-programs"), view(Win7Programs::ProgramsView)},
        {tr7("Internet Options"), QStringLiteral("seven-cpi-inetopts"), QStringLiteral("seven-cpi-inetopts"), self({QStringLiteral("--inetopts")})},
        {tr7("Color Management"), QStringLiteral("seven-cpi-color"), QStringLiteral("seven-cpi-color"), self({QStringLiteral("--colormgmt")})},
        {tr7("NVIDIA Control Panel"), QStringLiteral("seven-nvidia"), QStringLiteral("seven-nvidia"), program(QStringLiteral("seven-nvidia"))},
        {QStringLiteral("AMD Catalyst Control Center"), QStringLiteral("seven-ccc"), QStringLiteral("seven-ccc"), program(QStringLiteral("seven-ccc"))},
        {tr7("Intel® HD Graphics"), QStringLiteral("seven-igfx"), QStringLiteral("seven-igfx"), program(QStringLiteral("seven-igfx"))},
        {tr7("Windows Update"), QStringLiteral("seven-cpi-update"), QStringLiteral("seven-cpi-update"), view(Win7Update::UpdateView)},
        {tr7("Network and Sharing Center"), QStringLiteral("seven-cpi-network"), QStringLiteral("seven-cpi-network"), view(Win7Net::CenterView)},
        {tr7("Region and Language"), QStringLiteral("seven-cpi-region"), QStringLiteral("seven-cpi-region"), self({QStringLiteral("--region")})},
        {tr7("Windows Firewall"), QStringLiteral("seven-cpi-firewall"), QStringLiteral("seven-cpi-firewall"), view(Win7Firewall::StatusView)},
        {tr7("Mouse"), QStringLiteral("seven-cpi-mouse"), QStringLiteral("seven-cpi-mouse"), self({QStringLiteral("--mouse")})},
        {tr7("Personalization"), QStringLiteral("seven-cpi-personalize"), QStringLiteral("seven-cpi-personalize"), view(Win7Display::PersonalizeView)},
        {tr7("Default Programs"), QStringLiteral("seven-cpi-defaults"), QStringLiteral("seven-cpi-defaults"), view(Win7Programs::DefaultsView)},
        {tr7("System"), QStringLiteral("seven-cpi-system"), QStringLiteral("seven-cpi-system"), tool(QStringLiteral("seven-system"), QStringLiteral("kinfocenter"))},
        {tr7("Action Center"), QStringLiteral("seven-cpi-actioncenter"), QStringLiteral("seven-cpi-actioncenter"), view(Win7Action::CenterView)},
        {tr7("Fonts"), QStringLiteral("seven-cpi-fonts"), QStringLiteral("seven-cpi-fonts"), view(Win7Fonts::FontsView)},
        {tr7("Performance Information and Tools"), QStringLiteral("seven-cpi-perf"), QStringLiteral("seven-cpi-perf"), view(Win7Perf::PerfView)},
        {tr7("NVIDIA Control Panel"), QStringLiteral("seven-cpa-nvidia"), QStringLiteral("seven-cpa-nvidia"), program(QStringLiteral("nvidia-settings"))},
        {tr7("Desktop Gadgets"), QStringLiteral("gadgets"), QStringLiteral("gadgets"), program(QStringLiteral("seven-gadgets"), {QStringLiteral("--gallery")})},
        {tr7("Speech Recognition"), QStringLiteral("seven-snd-mic"), QStringLiteral("seven-snd-mic"), view(97)},
        {tr7("Folder Options"), QStringLiteral("seven-cpi-folderopts"), QStringLiteral("seven-cpi-folderopts"), self({QStringLiteral("--folder-options")})},
    };
    // the Linux nvidia-settings window is hidden when our Win7-style NVIDIA panel is installed (it still uses nvidia-settings underneath)
    if (QStandardPaths::findExecutable(QStringLiteral("nvidia-settings")).isEmpty()
        || QFileInfo::exists(QStringLiteral("seven-nvidia"))) {
        items.erase(std::remove_if(items.begin(), items.end(), [](const Item &i) {
                        return i.icon == QLatin1String("seven-cpa-nvidia");
                    }),
                    items.end());
    }
    {
        const GpuPanels g = gpuPanels();
        items.erase(std::remove_if(items.begin(), items.end(), [&g](const Item &i) {
                        return hiddenVendorPanel(i.icon, g);
                    }),
                    items.end());
    }
    // alphabetical like Windows: names in Latin letters (Flash Player, Java...) come first
    QCollator collator(QLocale{});
    std::sort(items.begin(), items.end(), [&collator](const Item &a, const Item &b) {
        const bool la = a.name.at(0).script() == QChar::Script_Latin, lb = b.name.at(0).script() == QChar::Script_Latin;
        if (la != lb) {
            return la;
        }
        return collator.compare(a.name, b.name) < 0;
    });
    return items;
}

/* ---------------- page pieces ---------------- */

/** Clickable row: icon + text, the text underlines on hover */
class ItemLink : public QWidget
{
public:
    ItemLink(const QIcon &icon, int iconSize, const QString &text, std::function<void()> open, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_open(std::move(open))
    {
        setCursor(Qt::PointingHandCursor);
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(2, 1, 2, 1);
        h->setSpacing(iconSize > 16 ? 8 : 5);
        auto *ic = new QLabel(this);
        ic->setPixmap(icon.pixmap(iconSize, iconSize));
        ic->setFixedSize(iconSize, iconSize);
        h->addWidget(ic, 0, Qt::AlignVCenter);
        m_label = new QLabel(text, this);
        m_label->setWordWrap(iconSize > 16);
        h->addWidget(m_label, 1, Qt::AlignVCenter);
        apply(false);
    }

protected:
    void enterEvent(QEnterEvent *) override
    {
        apply(true);
    }
    void leaveEvent(QEvent *) override
    {
        apply(false);
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && m_open) {
            m_open();
        }
    }

private:
    void apply(bool hover)
    {
        m_label->setStyleSheet(QStringLiteral("QLabel { color: %1; text-decoration: %2; }")
                                   .arg(hover ? QStringLiteral("#3399ff") : QStringLiteral("#1e6b1e"), hover ? QStringLiteral("underline") : QStringLiteral("none")));
    }
    std::function<void()> m_open;
    QLabel *m_label;
};

QWidget *categoryBlock(const CategoryInfo &c)
{
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(12);
    auto *icon = new QLabel;
    icon->setPixmap(QIcon::fromTheme(c.icon).pixmap(48, 48));
    icon->setFixedSize(52, 52);
    icon->setCursor(Qt::PointingHandCursor);
    h->addWidget(icon, 0, Qt::AlignTop);
    auto *v = new QVBoxLayout;
    v->setSpacing(1);
    auto *title = new Link(c.title, c.open, QStringLiteral("#1e7b1e"));
    QFont tf = title->font();
    tf.setPixelSize(16);
    title->setFont(tf);
    v->addWidget(title);
    for (const SubLink &l : c.links) {
        if (l.shield) {
            auto *row = new QHBoxLayout;
            row->setSpacing(3);
            auto *sh = new QLabel;
            sh->setPixmap(QIcon::fromTheme(QStringLiteral("seven-cp-shield")).pixmap(16, 16));
            row->addWidget(sh, 0, Qt::AlignTop);
            row->addWidget(new Link(l.text, l.open), 1);
            v->addLayout(row);
        } else {
            v->addWidget(new Link(l.text, l.open));
        }
    }
    v->addStretch(1);
    h->addLayout(v, 1);
    return w;
}

/* ---------------- category pages ("Appearance and Personalization" ...) ---------------- */

struct PageSection {
    QString icon; // 24 px
    QString title;
    std::function<void()> open;
    QList<SubLink> links;
};

struct CategoryPage {
    QString title;
    QList<PageSection> sections;
};

std::function<void()> shell(const QString &cmd)
{
    return [cmd] {
        QProcess::startDetached(QStringLiteral("sh"), {QStringLiteral("-c"), cmd});
    };
}

const QList<CategoryPage> &categoryPages()
{
    static QList<CategoryPage> pages{
        {tr7("Appearance and Personalization"),
         {
             {QStringLiteral("seven-cpi-personalize"), tr7("Personalization"), view(Win7Display::PersonalizeView),
              {{tr7("Change the theme"), view(Win7Display::PersonalizeView)},
               {tr7("Change desktop background"), view(Win7Background::BackgroundView)},
               {tr7("Change window glass colors"), [] { Win7Display::openWindowColor(); }},
               {tr7("Change sound effects"), self({QStringLiteral("--sound"), QStringLiteral("2")})},
               {tr7("Change screen saver"), kcm(QStringLiteral("kcm_screenlocker"))}}},
             {QStringLiteral("seven-cpi-display"), tr7("Display"), view(Win7Display::DisplayView),
              {{tr7("Make text and other items larger or smaller"), view(Win7Display::DisplayView)},
               {tr7("Adjust screen resolution"), view(Win7Display::ResolutionView)},
               {tr7("Connect to an external display"), [] { Win7Display::showDisplaySwitcher(); }}}},
             {QStringLiteral("seven-cpi-gadgets"), tr7("Desktop Gadgets"), program(QStringLiteral("seven-gadgets"), {QStringLiteral("--gallery")}),
              {{tr7("Add gadgets to the desktop"), program(QStringLiteral("seven-gadgets"), {QStringLiteral("--gallery")})}}},
             {QStringLiteral("seven-cpi-taskbar"), tr7("Taskbar and Start Menu"), [] { QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--taskbar-props")}); },
              {{tr7("Customize the Start menu"), [] { QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--taskbar-props"), QStringLiteral("start")}); }},
               {tr7("Customize icons on the taskbar"), view(Win7Tray::IconsView)},
               {tr7("Change picture on Start menu"), [] {
                    Win7Users::target().clear();
                    g_openView(Win7Users::PictureView);
                }}}},
             {QStringLiteral("seven-cpi-access"), tr7("Ease of Access Center"), view(Win7Access::CenterView),
              {{tr7("Optimize for blindness"), view(Win7Access::SeeView)},
               {tr7("Use screen reader"), view(Win7Access::NoDisplayView)},
               {tr7("Turn on easy access keys"), view(Win7Access::KeyboardView)},
               {tr7("Turn High Contrast on or off"), view(Win7Display::PersonalizeView)}}},
             {QStringLiteral("seven-cpi-folderopts"), tr7("Folder Options"), self({QStringLiteral("--folder-options")}),
              {{tr7("Specify single- or double-click to open"), self({QStringLiteral("--folder-options")})},
               {tr7("Show hidden files and folders"), self({QStringLiteral("--folder-options"), QString(), QStringLiteral("--tab"), QStringLiteral("1")})}}},
             {QStringLiteral("seven-cpi-fonts"), tr7("Fonts"), view(Win7Fonts::FontsView),
              {{tr7("Preview, delete, or show and hide fonts"), view(Win7Fonts::FontsView)},
               {tr7("Change Font Settings"), view(Win7Fonts::SettingsView)},
               {tr7("Adjust ClearType text"), self({QStringLiteral("--cttune")})}}},
         }},
        {tr7("System and Security"),
         {
             {QStringLiteral("seven-cpi-actioncenter"), tr7("Action Center"), view(Win7Action::CenterView),
              {{tr7("Review your computer's status and resolve issues"), view(Win7Action::CenterView)},
               {tr7("Restore your computer to an earlier time"), view(Win7Backup::RecoveryView)}}},
             {QStringLiteral("seven-cpi-firewall"), tr7("Windows Firewall"), view(Win7Firewall::StatusView),
              {{tr7("Check firewall status"), view(Win7Firewall::StatusView)},
               {tr7("Allow a program through Windows Firewall"), view(Win7Firewall::AllowedView)}}},
             {QStringLiteral("seven-cpi-system"), tr7("System"), tool(QStringLiteral("seven-system"), QStringLiteral("kinfocenter")),
              {{tr7("View amount of RAM and processor speed"), tool(QStringLiteral("seven-system"), QStringLiteral("kinfocenter"))},
               {tr7("See the name of this computer"), tool(QStringLiteral("seven-system"), QStringLiteral("kinfocenter"))},
               {tr7("Check the Windows Experience Index"), view(Win7Perf::PerfView)},
               {tr7("Device Manager"), tool(QStringLiteral("seven-devmgr"), QStringLiteral("kinfocenter")), true}}},
             {QStringLiteral("seven-cpi-update"), tr7("Windows Update"), view(Win7Update::UpdateView),
              {{tr7("Turn automatic updating on or off"), view(Win7Update::SettingsView)},
               {tr7("Check for updates"), [] {
                    Win7Update::Engine::self().check();
                    if (g_openView) {
                        g_openView(Win7Update::UpdateView);
                    }
                }},
               {tr7("View installed updates"), view(Win7Programs::UpdatesView)}}},
             {QStringLiteral("seven-cpi-power"), tr7("Power Options"), view(Win7Power::PlansView),
              {{tr7("Require a password when the computer wakes"), view(Win7Power::ButtonsView)},
               {tr7("Change what the power buttons do"), view(Win7Power::ButtonsView)},
               {tr7("Change when the computer sleeps"), editActivePlan()}}},
             {QStringLiteral("seven-cpi-backup"), tr7("Backup and Restore"), view(Win7Backup::BackupView),
              {{tr7("Back up your computer"), view(Win7Backup::BackupView)},
               {tr7("Restore data, files, or computer from backup"), self({QStringLiteral("--restore-files")})}}},
             {QStringLiteral("seven-cpi-admin"), tr7("Administrative Tools"), adminTools(),
              {{tr7("Free up disk space"), self({QStringLiteral("--cleanmgr")})},
               {tr7("Create and format hard disk partitions"), self({QStringLiteral("--diskmgmt")}), true},
               {tr7("View event logs"), self({QStringLiteral("--eventvwr")}), true}}},
         }},
        {tr7("Network and Internet"),
         {
             {QStringLiteral("seven-cpi-network"), tr7("Network and Sharing Center"), view(Win7Net::CenterView),
              {{tr7("View network status and tasks"), view(Win7Net::CenterView)},
               {tr7("Connect to a network"), [] { Win7Net::openNetworkFlyout(); }},
               {tr7("View network computers and devices"), program(QStringLiteral("dolphin"), {QStringLiteral("network:/")})}}},
             {QStringLiteral("seven-cpi-homegroup"), tr7("HomeGroup"), program(QStringLiteral("dolphin"), {QStringLiteral("smb:/")}),
              {{tr7("Choose homegroup and sharing options"), program(QStringLiteral("dolphin"), {QStringLiteral("smb:/")})}}},
             {QStringLiteral("seven-cpi-inetopts"), tr7("Internet Options"), self({QStringLiteral("--inetopts")}), {}},
         }},
        {tr7("Clock, Language, and Region"),
         {
             {QStringLiteral("seven-cpi-datetime"), tr7("Date and Time"), self({QStringLiteral("--datetime")}),
              {{tr7("Set the time and date"), self({QStringLiteral("--set-datetime")})},
               {tr7("Change the time zone"), self({QStringLiteral("--timezone")})},
               {tr7("Add clocks for different time zones"), self({QStringLiteral("--datetime"), QStringLiteral("1")})},
               {tr7("Add the Clock gadget to the desktop"), program(QStringLiteral("seven-gadgets"), {QStringLiteral("--add"), QStringLiteral("clock")})}}},
             {QStringLiteral("seven-cpi-region"), tr7("Region and Language"), self({QStringLiteral("--region")}),
              {{tr7("Change the date, time, or number format"), self({QStringLiteral("--region")})},
               {tr7("Change keyboards or other input methods"), self({QStringLiteral("--text-services")})}}},
         }},
        {tr7("Hardware and Sound"),
         {
             {QStringLiteral("seven-cpi-printers"), tr7("Devices and Printers"), view(Win7Dev::DevicesView),
              {{tr7("Add a device"), [] { Win7Dev::addDeviceWizard(nullptr, [] {}); }},
               {tr7("Add a printer"), [] { Win7Printer::addPrinterWizard(nullptr); }},
               {tr7("Mouse"), self({QStringLiteral("--mouse")})},
               {tr7("Device Manager"), tool(QStringLiteral("seven-devmgr"), QStringLiteral("kinfocenter")), true}}},
             {QStringLiteral("seven-cpi-autostart"), tr7("AutoPlay"), view(Win7Autoplay::AutoplayView),
              {{tr7("Change default settings for media or devices"), view(Win7Autoplay::AutoplayView)},
               {tr7("Play CDs or other media automatically"), view(Win7Autoplay::AutoplayView)}}},
             {QStringLiteral("seven-cpi-sound"), tr7("Sound"), self({QStringLiteral("--sound")}),
              {{tr7("Adjust system volume"), self({QStringLiteral("--sndvol")})},
               {tr7("Change system sounds"), self({QStringLiteral("--sound"), QStringLiteral("2")})},
               {tr7("Manage audio devices"), self({QStringLiteral("--sound")})}}},
             {QStringLiteral("seven-cpi-power"), tr7("Power Options"), view(Win7Power::PlansView),
              {{tr7("Change battery settings"), view(Win7Power::PlansView)},
               {tr7("Change what the power buttons do"), view(Win7Power::ButtonsView)},
               {tr7("Require a password when the computer wakes"), view(Win7Power::ButtonsView)},
               {tr7("Change when the computer sleeps"), editActivePlan()},
               {tr7("Choose a power plan"), view(Win7Power::PlansView)}}},
             {QStringLiteral("seven-cpi-display"), tr7("Display"), view(Win7Display::DisplayView),
              {{tr7("Make text and other items larger or smaller"), view(Win7Display::DisplayView)},
               {tr7("Adjust screen resolution"), view(Win7Display::ResolutionView)},
               {tr7("Connect to an external display"), [] { Win7Display::showDisplaySwitcher(); }},
               {tr7("Change the refresh rate of your monitor (flicker)"), view(Win7Display::ResolutionView)}}},
             {QStringLiteral("seven-nvidia"), tr7("NVIDIA Control Panel"), program(QStringLiteral("seven-nvidia")), {}},
             {QStringLiteral("seven-ccc"), QStringLiteral("AMD Catalyst Control Center"), program(QStringLiteral("seven-ccc")), {}},
             {QStringLiteral("seven-igfx"), tr7("Intel® HD Graphics"), program(QStringLiteral("seven-igfx")), {}},
         }},
        {tr7("Programs"),
         {
             {QStringLiteral("seven-cpi-programs"), tr7("Programs and Features"), view(Win7Programs::ProgramsView),
              {{tr7("Uninstall a program"), view(Win7Programs::ProgramsView)},
               {tr7("Turn Windows features on or off"), [] { Win7Programs::showFeatures(); }, true},
               {tr7("View installed updates"), view(Win7Programs::UpdatesView)},
               {tr7("Run programs made for previous versions of Windows"), program(QStringLiteral("winecfg"))},
               {tr7("How to install a program"), view(Win7Programs::ProgramsView)}}},
             {QStringLiteral("seven-cpi-defaults"), tr7("Default Programs"), view(Win7Programs::DefaultsView),
              {{tr7("Change default settings for media or devices", "autoplay task"), view(Win7Autoplay::AutoplayView)},
               {tr7("Make a file type always open in a specific program"), view(Win7Programs::AssocView)},
               {tr7("Set your default programs"), view(Win7Programs::SetDefaultsView)}}},
             {QStringLiteral("seven-cpi-gadgets"), tr7("Desktop Gadgets"), program(QStringLiteral("seven-gadgets"), {QStringLiteral("--gallery")}),
              {{tr7("Add gadgets to the desktop"), program(QStringLiteral("seven-gadgets"), {QStringLiteral("--gallery")})},
               {tr7("Uninstall a gadget"), program(QStringLiteral("seven-gadgets"), {QStringLiteral("--gallery")})}}},
         }},
        {tr7("User Accounts and Family Safety"),
         {
             {QStringLiteral("seven-cpi-users"), tr7("User Accounts"), view(Win7Users::HomeView),
              {{tr7("Change your account picture"), [] {
                    Win7Users::target().clear();
                    g_openView(Win7Users::PictureView);
                }},
               {tr7("Add or remove user accounts"), view(Win7Users::ManageView), true},
               {tr7("Change your Windows password"), view(Win7Users::HomeView)}}},
         }},
        {tr7("Ease of Access"),
         {
             {QStringLiteral("seven-cpi-access"), tr7("Ease of Access Center"), view(Win7Access::CenterView),
              {{tr7("Optimize for blindness"), view(Win7Access::SeeView)},
               {tr7("Use screen reader", "ease of access task"), view(Win7Access::NoDisplayView)},
               {tr7("Turn on easy access keys"), view(Win7Access::KeyboardView)},
               {tr7("Turn High Contrast on or off", "ease of access task"), view(Win7Display::PersonalizeView)}}},
             {QStringLiteral("seven-cpi-speech"), tr7("Speech Recognition"), view(97),
              {{tr7("Start speech recognition"), view(97)},
               {tr7("Set up a microphone"), self({QStringLiteral("--sound"), QStringLiteral("1")})}}},
         }},
    };
    static const bool pruned = [] {
        // vendor panels only for the GPUs that are really there
        const GpuPanels g = gpuPanels();
        for (CategoryPage &c : pages) {
            c.sections.erase(std::remove_if(c.sections.begin(), c.sections.end(), [&g](const PageSection &s) {
                                 return hiddenVendorPanel(s.icon, g);
                             }),
                             c.sections.end());
        }
        return true;
    }();
    Q_UNUSED(pruned);
    return pages;
}

/** Links separated by thin grey bars that wrap to the next line, like Windows 7 */
class LinkFlow : public QWidget
{
public:
    explicit LinkFlow(const QList<SubLink> &links, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        for (const SubLink &l : links) {
            auto *item = new QWidget(this);
            auto *h = new QHBoxLayout(item);
            h->setContentsMargins(0, 0, 0, 0);
            h->setSpacing(2);
            if (l.shield) {
                auto *sh = new QLabel(item);
                sh->setPixmap(QIcon::fromTheme(QStringLiteral("seven-cp-shield")).pixmap(16, 16));
                h->addWidget(sh);
            }
            auto *link = new Link(l.text, l.open, QStringLiteral("#0066cc"), item);
            link->setWordWrap(false);
            h->addWidget(link);
            m_items.append(item);
        }
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    }
    bool hasHeightForWidth() const override
    {
        return true;
    }
    int heightForWidth(int w) const override
    {
        return place(w, false);
    }
    QSize sizeHint() const override
    {
        const int w = width() > 50 ? width() : 600;
        return QSize(w, place(w, false));
    }
    QSize minimumSizeHint() const override
    {
        return QSize(100, 0);
    }

protected:
    void resizeEvent(QResizeEvent *) override
    {
        place(width(), true);
        updateGeometry();
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setPen(QColor(0xc8, 0xcc, 0xd4));
        for (const QRect &r : std::as_const(m_bars)) {
            p.drawLine(r.center().x(), r.top() + 3, r.center().x(), r.bottom() - 2);
        }
    }

private:
    int place(int w, bool apply) const
    {
        const int gap = 13;
        int x = 0, y = 0, lineH = 0;
        QList<QRect> bars;
        for (int i = 0; i < m_items.size(); ++i) {
            QWidget *it = m_items.at(i);
            const QSize sz = it->sizeHint();
            if (x > 0 && x + sz.width() > w) {
                x = 0;
                y += lineH + 1;
                lineH = 0;
            }
            if (apply) {
                it->setGeometry(x, y, sz.width(), sz.height());
            }
            x += sz.width();
            lineH = qMax(lineH, sz.height());
            if (i + 1 < m_items.size()) {
                bars.append(QRect(x, y, gap, sz.height())); // Windows 7 keeps the bar even at the end of a line
                x += gap;
            }
        }
        if (apply) {
            m_bars = bars;
        }
        return y + lineH;
    }
    QList<QWidget *> m_items;
    mutable QList<QRect> m_bars;
};

/** Section of a category page; Windows 7 draws a light blue box around the one under the mouse */
class HoverBox : public QWidget
{
public:
    HoverBox()
    {
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!underMouse()) {
            return;
        }
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, QColor(0xfa, 0xfc, 0xff));
        g.setColorAt(1, QColor(0xf0, 0xf6, 0xfe));
        p.setBrush(g);
        p.setPen(QColor(0xd4, 0xe4, 0xf7));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    }
    void enterEvent(QEnterEvent *) override
    {
        update();
    }
    void leaveEvent(QEvent *) override
    {
        update();
    }
};

QWidget *pageSection(const PageSection &sec)
{
    auto *w = new HoverBox;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(4, 3, 4, 4);
    h->setSpacing(8);
    auto *icon = new QLabel;
    icon->setPixmap(QIcon::fromTheme(sec.icon).pixmap(32, 32));
    icon->setFixedSize(34, 34);
    h->addWidget(icon, 0, Qt::AlignTop);
    auto *v = new QVBoxLayout;
    v->setSpacing(0);
    auto *title = new Link(sec.title, sec.open, QStringLiteral("#1e7b1e"));
    QFont tf = title->font();
    tf.setPixelSize(15);
    title->setFont(tf);
    v->addWidget(title);
    if (!sec.links.isEmpty()) {
        v->addWidget(new LinkFlow(sec.links));
    }
    h->addLayout(v, 1);
    return w;
}

enum View {
    CategoryView = 0,
    LargeIcons,
    SmallIcons,
    FirstCategoryPage = 100, // 100 + index into categoryPages()
};

QString viewName(int v)
{
    switch (v) {
    case LargeIcons:
        return tr7("Large icons");
    case SmallIcons:
        return tr7("Small icons");
    default:
        return tr7("Category");
    }
}

/* ---------------- window ---------------- */

class ControlPanel : public QMainWindow
{
public:
    explicit ControlPanel(bool allItems)
    {
        m_view = allItems ? SmallIcons : CategoryView;
        if (QCoreApplication::arguments().contains(QStringLiteral("--large"))) {
            m_view = LargeIcons;
        }
        setWindowTitle(tr7("Control Panel"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-cpi-controlpanel")));

        m_scroll = new QScrollArea(this);
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setWidgetResizable(true);
        m_scroll->viewport()->setObjectName(QStringLiteral("win7viewport"));
        m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: #ffffff; border: none; } #win7viewport { background: #ffffff; }"));
        setCentralWidget(m_scroll);
        m_commandBar = new QToolBar(this);
        m_commandBar->setMovable(false);
        m_commandBar->setFloatable(false);
        m_commandBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_commandBar->setContextMenuPolicy(Qt::PreventContextMenu);
        addToolBar(Qt::TopToolBarArea, m_commandBar);
        m_commandBar->hide();

        m_header = makeWin7Header(this, {}, QStringLiteral("seven-cpi-controlpanel"), tr7("Search Control Panel"));
        connect(m_header.refresh, &QToolButton::clicked, this, [this] {
            rebuild();
        });
        connect(m_header.search, &QLineEdit::textChanged, this, [this](const QString &text) {
            if (m_view == Win7Net::ConnectionsView && m_connections) {
                m_connections->setFilter(text.trimmed());
                return;
            }
            if (m_view == Win7Dev::DevicesView && m_devices) {
                m_devices->setFilter(text.trimmed());
                return;
            }
            if (m_programs && (m_view == Win7Programs::ProgramsView || m_view == Win7Programs::UpdatesView)) {
                m_programs->setFilter(text.trimmed());
                return;
            }
            if (m_assoc && m_view == Win7Programs::AssocView) {
                m_assoc->setFilter(text.trimmed());
                return;
            }
            if (m_fonts && (m_view == Win7Fonts::FontsView || m_view == Win7Fonts::FamilyView)) {
                m_fonts->setFilter(text.trimmed());
                return;
            }
            m_filter = text.trimmed();
            if (!m_filter.isEmpty() && m_view != LargeIcons && m_view != SmallIcons) {
                m_view = SmallIcons; // Windows 7 shows matching items while you type
            }
            rebuild();
        });
        connect(m_header.navs->back(), &QAbstractButton::clicked, this, [this] {
            goBack();
        });
        connect(m_header.navs->forward(), &QAbstractButton::clicked, this, [this] {
            if (!m_future.isEmpty()) {
                m_history.append(m_view);
                m_view = m_future.takeLast();
                rebuild(false);
            }
        });
        connect(m_header.history, &QToolButton::clicked, this, [this] {
            QMenu menu(this);
            menu.addAction(QIcon::fromTheme(QStringLiteral("seven-cpi-controlpanel")), tr7("Control Panel"), this, [this] {
                setView(CategoryView);
            });
            menu.addAction(QIcon::fromTheme(QStringLiteral("seven-cpi-controlpanel")), tr7("All Control Panel Items"), this, [this] {
                setView(SmallIcons);
            });
            // Windows 7: the "▾" stays pressed while the list is open
            auto *band = static_cast<Win7BandButton *>(m_header.history);
            band->menuOpen = true;
            band->update();
            menu.exec(m_header.history->mapToGlobal(QPoint(0, m_header.history->height())));
            band->menuOpen = false;
            band->update();
            if (band->partner) {
                band->partner->update();
            }
        });
        g_openCategoryPage = [this](int index) {
            setView(FirstCategoryPage + index);
        };
        g_openView = [this](int view) {
            setView(view);
        };
        Aero::makeInsetWindow(this, nullptr, m_header.widget, nullptr);
        rebuild(false);
        resize(980, 640);
    }

private:
    static bool isPowerView(int v)
    {
        return v == Win7Power::PlansView || v == Win7Power::ButtonsView
            || (v >= Win7Power::EditView && v < Win7Power::EditView + Win7Power::plans().size());
    }

    static bool isDisplayView(int v)
    {
        return v == Win7Display::PersonalizeView || v == Win7Display::DisplayView || v == Win7Display::ResolutionView
            || v == Win7Background::BackgroundView || v == Win7WindowColor::ColorView;
    }

    static bool isFirewallView(int v)
    {
        return v == Win7Firewall::StatusView || v == Win7Firewall::SettingsView || v == Win7Firewall::AllowedView;
    }

    static constexpr int SpeechView = 97;

    static bool isTrayView(int v)
    {
        return v == Win7Tray::IconsView || v == Win7Tray::SystemView;
    }

    static bool isProgramsView(int v)
    {
        return v >= Win7Programs::ProgramsView && v <= Win7Programs::ProgramAssocView;
    }

    static bool isFontsView(int v)
    {
        return v == Win7Fonts::FontsView || v == Win7Fonts::FamilyView || v == Win7Fonts::SettingsView;
    }

    static bool isNetView(int v)
    {
        return v == Win7Net::CenterView || v == Win7Net::ConnectionsView || v == Win7Net::SharingView;
    }

    void goBack()
    {
        if (m_history.isEmpty()) {
            setView(Win7Power::PlansView);
            return;
        }
        m_future.append(m_view);
        m_view = m_history.takeLast();
        rebuild(false);
    }

    void setView(int view)
    {
        if (view == m_view && m_filter.isEmpty()) {
            rebuild(); // e.g. a power page reloading itself
            return;
        }
        Win7Sound::playNavigationSound();
        m_history.append(m_view);
        m_future.clear();
        m_view = view;
        m_filter.clear();
        m_header.search->blockSignals(true);
        m_header.search->clear();
        m_header.search->blockSignals(false);
        rebuild(false);
    }

    void rebuild(bool keepScroll = true)
    {
        Q_UNUSED(keepScroll)
        // Address bar: "▸ Control Panel ▸" or "▸ Control Panel ▸ All Control Panel Items ▸"
        QList<Crumbs::Crumb> crumbs{{tr7("Control Panel"), [this] {
                                         setView(CategoryView);
                                     }}};
        const bool power = isPowerView(m_view);
        if (Win7Games::isGamesView(m_view)) {
            crumbs = {{tr7("Games"), nullptr}};
        } else if (m_view >= FirstCategoryPage) {
            crumbs.append({categoryPages().at(m_view - FirstCategoryPage).title, nullptr});
        } else if (isFirewallView(m_view)) {
            crumbs.append({tr7("System and Security"), [this] {
                               setView(FirstCategoryPage + 1);
                           }});
            crumbs.append({tr7("Windows Firewall"), m_view == Win7Firewall::StatusView ? std::function<void()>() : [this] {
                               setView(Win7Firewall::StatusView);
                           }});
            if (m_view == Win7Firewall::AllowedView) {
                crumbs.append({tr7("Allowed Programs"), nullptr});
            }
            if (m_view == Win7Firewall::SettingsView) {
                crumbs.append({tr7("Customize settings"), nullptr});
            }
        } else if (isDisplayView(m_view)) {
            // Windows 7 (opened from the desktop menu): Control Panel > Appearance and Personalization > ...
            crumbs.append({tr7("Appearance and Personalization"), [this] {
                               setView(FirstCategoryPage + 0);
                           }});
            if (m_view == Win7Display::PersonalizeView) {
                crumbs.append({tr7("Personalization"), nullptr});
            } else if (m_view == Win7Background::BackgroundView) {
                crumbs.append({tr7("Personalization"), [this] {
                                   setView(Win7Display::PersonalizeView);
                               }});
                crumbs.append({tr7("Desktop Background"), nullptr});
            } else if (m_view == Win7WindowColor::ColorView) {
                crumbs.append({tr7("Personalization"), [this] {
                                   setView(Win7Display::PersonalizeView);
                               }});
                crumbs.append({tr7("Window Color and Appearance"), nullptr});
            } else {
                crumbs.append({tr7("Display"), m_view == Win7Display::DisplayView ? std::function<void()>() : [this] {
                                   setView(Win7Display::DisplayView);
                               }});
                if (m_view == Win7Display::ResolutionView) {
                    crumbs.append({tr7("Screen Resolution"), nullptr});
                }
            }
        } else if (m_view == Win7Dev::DevicesView || m_view == Win7Autoplay::AutoplayView) {
            crumbs.append({tr7("Hardware and Sound"), [this] {
                               setView(FirstCategoryPage + 4);
                           }});
            crumbs.append({m_view == Win7Dev::DevicesView ? tr7("Devices and Printers") : tr7("AutoPlay"), nullptr});
        } else if (m_view == SpeechView) {
            crumbs.append({tr7("Ease of Access"), nullptr});
            crumbs.append({tr7("Speech Recognition"), nullptr});
        } else if (isTrayView(m_view)) {
            crumbs.append({tr7("All Control Panel Items"), [this] {
                               setView(SmallIcons);
                           }});
            crumbs.append({tr7("Notification Area Icons"), m_view == Win7Tray::IconsView ? std::function<void()>() : [this] {
                               setView(Win7Tray::IconsView);
                           }});
            if (m_view == Win7Tray::SystemView) {
                crumbs.append({tr7("System Icons"), nullptr});
            }
        } else if (Win7Access::isAccessView(m_view)) {
            crumbs.append({tr7("Ease of Access"), [this] {
                               setView(FirstCategoryPage + 7);
                           }});
            crumbs.append({tr7("Ease of Access Center"), m_view == Win7Access::CenterView ? std::function<void()>() : [this] {
                               setView(Win7Access::CenterView);
                           }});
            if (m_view == Win7Access::MouseKeysView || m_view == Win7Access::StickyView || m_view == Win7Access::FilterView) {
                crumbs.append({tr7("Make the keyboard easier to use"), [this] {
                                   setView(Win7Access::KeyboardView);
                               }});
            }
            if (m_view != Win7Access::CenterView) {
                crumbs.append({Win7Access::viewTitle(m_view), nullptr});
            }
        } else if (Win7Users::isUsersView(m_view)) {
            crumbs.append({tr7("User Accounts and Family Safety"), [this] {
                               setView(FirstCategoryPage + 6);
                           }});
            crumbs.append({tr7("User Accounts"), m_view == Win7Users::HomeView ? std::function<void()>() : [this] {
                               setView(Win7Users::HomeView);
                           }});
            if (m_view != Win7Users::HomeView) {
                const bool managing = m_view == Win7Users::ManageView || m_view == Win7Users::CreateView || m_view == Win7Users::AccountView || m_view == Win7Users::GuestView
                    || m_view == Win7Users::DeleteView || !Win7Users::target().isEmpty();
                if (managing && m_view != Win7Users::ManageView) {
                    crumbs.append({tr7("Manage Accounts"), [this] {
                                       setView(Win7Users::ManageView);
                                   }});
                }
                crumbs.append({Win7Users::viewTitle(m_view), nullptr});
            }
        } else if (isProgramsView(m_view)) {
            crumbs.append({tr7("Programs"), [this] {
                               setView(FirstCategoryPage + 5);
                           }});
            const bool features = m_view == Win7Programs::ProgramsView || m_view == Win7Programs::UpdatesView;
            const int parent = features ? Win7Programs::ProgramsView : Win7Programs::DefaultsView;
            crumbs.append({features ? tr7("Programs and Features") : tr7("Default Programs"), m_view == parent ? std::function<void()>() : [this, parent] {
                               setView(parent);
                           }});
            if (m_view == Win7Programs::UpdatesView) {
                crumbs.append({tr7("Installed Updates"), nullptr});
            } else if (m_view == Win7Programs::SetDefaultsView || m_view == Win7Programs::ProgramAssocView) {
                crumbs.append({tr7("Set your default programs"), m_view == Win7Programs::SetDefaultsView ? std::function<void()>() : [this] {
                                   setView(Win7Programs::SetDefaultsView);
                               }});
                if (m_view == Win7Programs::ProgramAssocView) {
                    crumbs.append({tr7("Set Associations"), nullptr});
                }
            } else if (m_view == Win7Programs::AssocView) {
                crumbs.append({tr7("Set Associations"), nullptr});
            }
        } else if (m_view == Win7Backup::BackupView) {
            crumbs.append({tr7("System and Security"), [this] {
                               setView(FirstCategoryPage + 1);
                           }});
            crumbs.append({tr7("Backup and Restore"), nullptr});
        } else if (Win7Backup::isBackupView(m_view)) {
            // the address bar of the recovery pages keeps the two last places, like Windows 7 ("« Recovery ▸ Advanced recovery methods")
            static const QHash<int, QPair<int, QString>> parent{{Win7Backup::AdvancedView, {Win7Backup::RecoveryView, tr7("Recovery")}},
                                                                {Win7Backup::UserFilesView, {Win7Backup::AdvancedView, tr7("Advanced Recovery Methods")}},
                                                                {Win7Backup::RestartView, {Win7Backup::UserFilesView, tr7("Backup and Restore", "user files backup")}}};
            if (m_view == Win7Backup::RecoveryView) {
                crumbs.append({tr7("All Control Panel Items"), [this] {
                                   setView(SmallIcons);
                               }});
            } else {
                const auto up = parent.value(m_view);
                crumbs.append({up.second, [this, up] {
                                   setView(up.first);
                               }});
            }
            crumbs.append({Win7Backup::viewTitle(m_view), nullptr});
        } else if (Win7Perf::isPerfView(m_view)) {
            crumbs.append({tr7("All Control Panel Items"), [this] {
                               setView(SmallIcons);
                           }});
            crumbs.append({Win7Perf::viewTitle(Win7Perf::PerfView), m_view == Win7Perf::PerfView ? std::function<void()>() : [this] {
                               setView(Win7Perf::PerfView);
                           }});
            if (m_view != Win7Perf::PerfView) {
                crumbs.append({Win7Perf::viewTitle(m_view), nullptr});
            }
        } else if (Win7Action::isActionView(m_view)) {
            crumbs.append({tr7("System and Security"), [this] {
                               setView(FirstCategoryPage + 1);
                           }});
            crumbs.append({tr7("Action Center"), m_view == Win7Action::CenterView ? std::function<void()>() : [this] {
                               setView(Win7Action::CenterView);
                           }});
            if (m_view != Win7Action::CenterView) {
                crumbs.append({Win7Action::viewTitle(m_view), nullptr});
            }
        } else if (Win7Update::isUpdateView(m_view)) {
            crumbs.append({tr7("System and Security"), [this] {
                               setView(FirstCategoryPage + 1);
                           }});
            crumbs.append({tr7("Windows Update"), m_view == Win7Update::UpdateView ? std::function<void()>() : [this] {
                               setView(Win7Update::UpdateView);
                           }});
            if (m_view != Win7Update::UpdateView) {
                crumbs.append({Win7Update::viewTitle(m_view), nullptr});
            }
        } else if (isFontsView(m_view)) {
            crumbs.append({tr7("All Control Panel Items"), [this] {
                               setView(SmallIcons);
                           }});
            crumbs.append({tr7("Fonts"), m_view == Win7Fonts::FontsView ? std::function<void()>() : [this] {
                               setView(Win7Fonts::FontsView);
                           }});
            if (m_view == Win7Fonts::FamilyView) {
                crumbs.append({Win7Fonts::openedFamily(), nullptr});
            } else if (m_view == Win7Fonts::SettingsView) {
                crumbs.append({tr7("Font settings"), nullptr});
            }
        } else if (isNetView(m_view)) {
            crumbs.append({tr7("Network and Internet"), [this] {
                               setView(FirstCategoryPage + 2);
                           }});
            if (m_view == Win7Net::SharingView) {
                crumbs.append({tr7("Network and Sharing Center"), [this] {
                                   setView(Win7Net::CenterView);
                               }});
                crumbs.append({tr7("Advanced sharing settings"), nullptr});
            } else {
                crumbs.append({m_view == Win7Net::CenterView ? tr7("Network and Sharing Center") : tr7("Network Connections"), nullptr});
            }
        } else if (power) {
            crumbs.append({tr7("All Control Panel Items"), [this] {
                               setView(SmallIcons);
                           }});
            crumbs.append({tr7("Power Options"), m_view == Win7Power::PlansView ? std::function<void()>() : [this] {
                               setView(Win7Power::PlansView);
                           }});
            if (m_view != Win7Power::PlansView) {
                crumbs.append({m_view == Win7Power::ButtonsView ? tr7("System Settings") : tr7("Edit Plan Settings"), nullptr});
            }
        } else if (m_view != CategoryView) {
            crumbs.append({tr7("All Control Panel Items"), nullptr});
        }
        auto *newCrumbs = new Crumbs(crumbs, m_header.crumbs->parentWidget());
        newCrumbs->setArrowMenu([this](QMenu *menu, int after) {
            if (after < 0) {
                menu->addAction(tr7("Control Panel"), this, [this] {
                    setView(CategoryView);
                });
                return;
            }
            for (const auto *col : {&leftColumn(), &rightColumn()}) {
                for (const CategoryInfo &c : *col) {
                    menu->addAction(QIcon::fromTheme(c.icon), c.title, c.open);
                }
            }
            menu->addSeparator();
            menu->addAction(tr7("All Control Panel Items"), this, [this] {
                setView(SmallIcons);
            });
        });
        m_header.crumbs->parentWidget()->layout()->replaceWidget(m_header.crumbs, newCrumbs);
        m_header.crumbs->deleteLater();
        m_header.crumbs = newCrumbs;
        m_header.navs->back()->setEnabled(!m_history.isEmpty());
        m_header.navs->forward()->setEnabled(!m_future.isEmpty());
        // the address bar shows the icon of the current place
        QString placeIcon = QStringLiteral("seven-cpi-controlpanel");
        if (power) {
            placeIcon = QStringLiteral("seven-cpi-power");
        } else if (m_view == SpeechView) {
            placeIcon = QStringLiteral("seven-snd-mic");
        } else if (m_view == Win7Dev::DevicesView) {
            placeIcon = QStringLiteral("seven-cpi-printers");
        } else if (m_view == Win7Autoplay::AutoplayView) {
            placeIcon = QStringLiteral("seven-cpi-autostart");
        } else if (isTrayView(m_view)) {
            placeIcon = QStringLiteral("seven-cpi-trayicons");
        } else if (isFirewallView(m_view)) {
            placeIcon = QStringLiteral("seven-cpi-firewall");
        } else if (isDisplayView(m_view)) {
            placeIcon = (m_view == Win7Display::PersonalizeView || m_view == Win7Background::BackgroundView || m_view == Win7WindowColor::ColorView)
                ? QStringLiteral("seven-cpi-personalize")
                                                                                                                : QStringLiteral("seven-cpi-display");
        } else if (Win7Access::isAccessView(m_view)) {
            placeIcon = QStringLiteral("seven-eoa");
        } else if (Win7Users::isUsersView(m_view)) {
            placeIcon = QStringLiteral("seven-cpi-users");
        } else if (isProgramsView(m_view)) {
            placeIcon = m_view <= Win7Programs::UpdatesView ? QStringLiteral("seven-cpi-programs") : QStringLiteral("seven-cpi-defaults");
        } else if (Win7Backup::isBackupView(m_view)) {
            placeIcon = m_view == Win7Backup::BackupView ? QStringLiteral("seven-cpi-backup") : QStringLiteral("seven-cpi-recovery");
        } else if (Win7Update::isUpdateView(m_view)) {
            placeIcon = QStringLiteral("seven-cpi-update");
        } else if (Win7Action::isActionView(m_view)) {
            placeIcon = QStringLiteral("seven-cpi-actioncenter");
        } else if (Win7Perf::isPerfView(m_view)) {
            placeIcon = QStringLiteral("seven-cpi-perf");
        } else if (Win7Games::isGamesView(m_view)) {
            placeIcon = QStringLiteral("seven-games");
        } else if (isFontsView(m_view)) {
            placeIcon = QStringLiteral("seven-cpi-fonts");
        } else if (isNetView(m_view)) {
            placeIcon = m_view != Win7Net::ConnectionsView ? QStringLiteral("seven-net-center") : QStringLiteral("seven-net-computers");
        } else if (m_view >= FirstCategoryPage) {
            for (const auto *col : {&leftColumn(), &rightColumn()}) {
                for (const CategoryInfo &c : *col) {
                    if (c.title == categoryPages().at(m_view - FirstCategoryPage).title) {
                        placeIcon = c.icon;
                    }
                }
            }
        }
        m_header.icon->setPixmap(QIcon::fromTheme(placeIcon).pixmap(16, 16));
        m_commandBar->setVisible(m_view == Win7Net::ConnectionsView || m_view == Win7Dev::DevicesView);
        m_connections = nullptr;
        m_devices = nullptr;
        m_programs = nullptr;
        m_assoc = nullptr;
        m_fonts = nullptr;
        m_header.search->setPlaceholderText(m_view == Win7Net::ConnectionsView ? tr7("Search: Network Connections")
                                                : m_view == Win7Dev::DevicesView ? tr7("Search: Devices and Printers")
                                                : m_view == Win7Games::GamesView ? tr7("Search: Games")
                                                : m_view == Win7Programs::ProgramsView ? tr7("Search: Programs and Features")
                                                : m_view == Win7Programs::UpdatesView ? tr7("Search: Installed Updates")
                                                : m_view == Win7Programs::AssocView ? tr7("Search: Set Associations")
                                                : (m_view == Win7Fonts::FontsView || m_view == Win7Fonts::FamilyView) ? tr7("Search: Fonts")
                                                                                 : tr7("Search Control Panel"));
        if (m_view == Win7Dev::DevicesView) {
            setWindowTitle(tr7("Devices and Printers"));
            m_devices = new Win7Dev::DevicesPage(m_commandBar, {[this](int v) {
                                                                    setView(v);
                                                                },
                                                                [this] {
                                                                    close();
                                                                }});
            m_scroll->setWidget(m_devices);
            return;
        }
        if (Win7Access::isAccessView(m_view)) {
            setWindowTitle(Win7Access::viewTitle(m_view));
            m_scroll->setWidget(Win7Access::build(m_view,
                                                  {[this](int v) {
                                                       setView(v);
                                                   },
                                                   [this] {
                                                       setView(CategoryView);
                                                   },
                                                   [this] {
                                                       if (g_launchView == m_view) {
                                                           close();
                                                       } else {
                                                           goBack();
                                                       }
                                                   },
                                                   [](const QStringList &args) {
                                                       QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
                                                   }},
                                                  SpeechView));
            return;
        }
        if (Win7Backup::isBackupView(m_view)) {
            setWindowTitle(Win7Backup::viewTitle(m_view));
            m_scroll->setWidget(Win7Backup::build(m_view, {[this](int v) {
                                                               setView(v);
                                                           },
                                                           [this] {
                                                               setView(CategoryView);
                                                           },
                                                           [this] {
                                                               goBack();
                                                           },
                                                           [this](int v) {
                                                               setView(v);
                                                           }}));
            return;
        }
        if (Win7Users::isUsersView(m_view)) {
            setWindowTitle(Win7Users::viewTitle(m_view));
            m_scroll->setWidget(Win7Users::build(m_view, {[this](int v) {
                                                               setView(v);
                                                           },
                                                           [this] {
                                                               setView(CategoryView);
                                                           },
                                                           [this] {
                                                               goBack();
                                                           }}));
            return;
        }
        if (isProgramsView(m_view)) {
            const Win7Programs::Nav nav{[this](int v) {
                                            setView(v);
                                        },
                                        [this] {
                                            setView(CategoryView);
                                        },
                                        [this] {
                                            if (g_launchView == m_view) {
                                                close();
                                            } else {
                                                goBack();
                                            }
                                        }};
            switch (m_view) {
            case Win7Programs::ProgramsView:
            case Win7Programs::UpdatesView:
                setWindowTitle(m_view == Win7Programs::ProgramsView ? tr7("Programs and Features") : tr7("Installed Updates"));
                m_programs = new Win7Programs::ProgramsPage(m_view == Win7Programs::UpdatesView, nav);
                m_scroll->setWidget(m_programs);
                break;
            case Win7Programs::DefaultsView:
                setWindowTitle(tr7("Default Programs"));
                m_scroll->setWidget(Win7Programs::buildDefaultsPage(nav, [this] {
                    setView(Win7Autoplay::AutoplayView);
                }));
                break;
            case Win7Programs::SetDefaultsView:
                setWindowTitle(tr7("Set your default programs"));
                m_scroll->setWidget(new Win7Programs::SetDefaultsPage(nav));
                break;
            case Win7Programs::AssocView:
                setWindowTitle(tr7("Set Associations"));
                m_assoc = new Win7Programs::AssocPage(nav);
                m_scroll->setWidget(m_assoc);
                break;
            default:
                setWindowTitle(tr7("Set Associations"));
                m_scroll->setWidget(Win7Programs::buildProgramAssocPage(nav));
                break;
            }
            return;
        }
        if (Win7Games::isGamesView(m_view)) {
            setWindowTitle(tr7("Games"));
            m_scroll->setWidget(Win7Games::buildGamesPage());
            return;
        }
        if (Win7Perf::isPerfView(m_view)) {
            setWindowTitle(Win7Perf::viewTitle(m_view));
            m_scroll->setWidget(Win7Perf::build(m_view, {[this](int v) {
                                                             setView(v);
                                                         },
                                                         [this] {
                                                             setView(CategoryView);
                                                         }}));
            return;
        }
        if (Win7Action::isActionView(m_view)) {
            setWindowTitle(Win7Action::viewTitle(m_view));
            m_scroll->setWidget(Win7Action::build(m_view, {[this](int v) {
                                                               setView(v);
                                                           },
                                                           [this] {
                                                               setView(CategoryView);
                                                           },
                                                           [this] {
                                                               if (g_launchView == m_view) {
                                                                   close();
                                                               } else {
                                                                   goBack();
                                                               }
                                                           }}));
            return;
        }
        if (Win7Update::isUpdateView(m_view)) {
            setWindowTitle(Win7Update::viewTitle(m_view));
            m_scroll->setWidget(Win7Update::build(m_view, {[this](int v) {
                                                               setView(v);
                                                           },
                                                           [this] {
                                                               setView(CategoryView);
                                                           },
                                                           [this] {
                                                               if (g_launchView == m_view) {
                                                                   close();
                                                               } else {
                                                                   goBack();
                                                               }
                                                           }}));
            return;
        }
        if (isFontsView(m_view)) {
            const Win7Fonts::Nav nav{[this](int v) {
                                         setView(v);
                                     },
                                     [this] {
                                         setView(CategoryView);
                                     },
                                     [this] {
                                         if (g_launchView == m_view) {
                                             close();
                                         } else {
                                             goBack();
                                         }
                                     },
                                     [](const QStringList &args) {
                                         QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
                                     }};
            if (m_view == Win7Fonts::SettingsView) {
                setWindowTitle(tr7("Font settings"));
                m_scroll->setWidget(Win7Fonts::buildSettingsPage(nav));
            } else {
                setWindowTitle(m_view == Win7Fonts::FamilyView ? Win7Fonts::openedFamily() : tr7("Fonts"));
                m_fonts = new Win7Fonts::FontsPage(m_view == Win7Fonts::FamilyView, nav);
                m_scroll->setWidget(m_fonts);
            }
            return;
        }
        if (m_view == Win7Autoplay::AutoplayView) {
            setWindowTitle(tr7("AutoPlay"));
            m_scroll->setWidget(Win7Autoplay::buildPage({[this] {
                if (g_launchView == Win7Autoplay::AutoplayView) {
                    close();
                } else {
                    goBack();
                }
            }}));
            return;
        }
        if (m_view == SpeechView) {
            setWindowTitle(tr7("Speech Recognition"));
            m_scroll->setWidget(Win7Sound::buildSpeechPage([] {
                QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--tts")});
            }));
            return;
        }
        if (isTrayView(m_view)) {
            const Win7Tray::Nav nav{[this](int v) {
                                        setView(v);
                                    },
                                    [this] {
                                        if (g_launchView >= 0) { // opened from the tray's "Customize..."
                                            close();
                                        } else {
                                            goBack();
                                        }
                                    }};
            setWindowTitle(tr7("Notification Area Icons"));
            m_scroll->setWidget(m_view == Win7Tray::IconsView ? Win7Tray::buildIconsPage(nav) : Win7Tray::buildSystemPage(nav));
            return;
        }
        if (isFirewallView(m_view)) {
            const Win7Firewall::Nav nav{[this](int v) {
                                            setView(v);
                                        },
                                        [this] {
                                            setView(CategoryView);
                                        },
                                        [this] {
                                            goBack();
                                        },
                                        [this] {
                                            setView(Win7Net::CenterView);
                                        }};
            if (m_view == Win7Firewall::StatusView) {
                setWindowTitle(tr7("Windows Firewall"));
                m_scroll->setWidget(Win7Firewall::buildStatusPage(nav));
            } else if (m_view == Win7Firewall::AllowedView) {
                setWindowTitle(tr7("Allowed Programs"));
                m_scroll->setWidget(Win7Firewall::buildAllowedPage(nav));
            } else {
                setWindowTitle(tr7("Customize settings"));
                m_scroll->setWidget(Win7Firewall::buildSettingsPage(nav));
            }
            return;
        }
        if (isDisplayView(m_view)) {
            const Win7Display::Nav nav{[this](int v) {
                                           setView(v);
                                       },
                                       [this] {
                                           setView(CategoryView);
                                       },
                                       [this] {
                                           goBack();
                                       },
                                       [this] {
                                           setView(Win7Power::PlansView);
                                       }};
            if (m_view == Win7Display::PersonalizeView) {
                setWindowTitle(tr7("Personalization"));
                m_scroll->setWidget(Win7Display::buildPersonalizePage(nav));
            } else if (m_view == Win7Background::BackgroundView) {
                setWindowTitle(tr7("Desktop Background"));
                auto *page = new Win7Background::BackgroundPage;
                page->back = [this] {
                    setView(Win7Display::PersonalizeView);
                };
                m_scroll->setWidget(page);
            } else if (m_view == Win7WindowColor::ColorView) {
                setWindowTitle(tr7("Window Color and Appearance"));
                auto *page = new Win7WindowColor::ColorPage;
                page->back = [this] {
                    setView(Win7Display::PersonalizeView);
                };
                m_scroll->setWidget(page);
            } else if (m_view == Win7Display::DisplayView) {
                setWindowTitle(tr7("Display"));
                m_scroll->setWidget(Win7Display::buildDisplayPage(nav));
            } else {
                setWindowTitle(tr7("Screen Resolution"));
                m_scroll->setWidget(Win7Display::buildResolutionPage(nav));
            }
            return;
        }
        if (isNetView(m_view)) {
            if (m_view == Win7Net::CenterView) {
                setWindowTitle(tr7("Network and Sharing Center"));
                m_scroll->setWidget(Win7Net::buildCenterPage({[this](int v) {
                                                                  setView(v);
                                                              },
                                                              [this] {
                                                                  setView(CategoryView);
                                                              }}));
            } else if (m_view == Win7Net::SharingView) {
                setWindowTitle(tr7("Advanced sharing settings"));
                auto *page = new Win7Sharing::SharingPage;
                page->back = [this] {
                    setView(Win7Net::CenterView);
                };
                m_scroll->setWidget(page);
            } else {
                setWindowTitle(tr7("Network Connections"));
                m_connections = new Win7Net::ConnectionsPage(m_commandBar, [this] {
                    close();
                });
                m_scroll->setWidget(m_connections);
            }
            return;
        }
        if (power) {
            setWindowTitle(m_view == Win7Power::PlansView     ? tr7("Power Options")
                               : m_view == Win7Power::ButtonsView ? tr7("System Settings")
                                                                  : tr7("Edit Plan Settings"));
            const Win7Power::Nav nav{[this](int v) {
                                         setView(v);
                                     },
                                     [this] {
                                         goBack();
                                     },
                                     [this] {
                                         setView(CategoryView);
                                     }};
            m_scroll->setWidget(m_view == Win7Power::PlansView     ? Win7Power::buildPlansPage(nav)
                                : m_view == Win7Power::ButtonsView ? Win7Power::buildButtonsPage(nav)
                                                                   : Win7Power::buildEditPage(m_view - Win7Power::EditView, nav));
            return;
        }
        setWindowTitle(m_view >= FirstCategoryPage ? categoryPages().at(m_view - FirstCategoryPage).title
                           : m_view == CategoryView ? tr7("Control Panel")
                                                    : tr7("All Control Panel Items"));
        if (m_view >= FirstCategoryPage) {
            m_scroll->setWidget(buildCategoryPage(categoryPages().at(m_view - FirstCategoryPage)));
            return;
        }

        auto *page = new QWidget;
        page->setObjectName(QStringLiteral("win7page"));
        page->setAttribute(Qt::WA_StyledBackground, true);
        page->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
        // Windows 7 (measured): the category view is a column at most 698 px wide, centred in the window;
        // the icon views use the whole width
        auto *outerH = new QHBoxLayout(page);
        outerH->setContentsMargins(0, 0, 0, 0);
        auto *column = new QWidget;
        if (m_view == CategoryView) {
            column->setMaximumWidth(698);
            outerH->addStretch(1);
            outerH->addWidget(column, 100);
            outerH->addStretch(1);
        } else {
            outerH->addWidget(column, 1);
        }
        auto *v = new QVBoxLayout(column);
        v->setContentsMargins(m_view == CategoryView ? 0 : 40, 14, m_view == CategoryView ? 0 : 40, 20);
        v->setSpacing(0);

        // Title and "View by: Category ▾"
        auto *top = new QHBoxLayout;
        auto *title = new QLabel(tr7("Adjust your computer's settings"));
        QFont tf = title->font();
        tf.setPixelSize(16);
        title->setFont(tf);
        title->setStyleSheet(QStringLiteral("color: #1e3287;"));
        top->addWidget(title, 1);
        top->addWidget(grey(tr7("View by:")));
        auto *viewBy = new QToolButton;
        viewBy->setText(viewName(m_view));
        viewBy->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        viewBy->setPopupMode(QToolButton::InstantPopup);
        viewBy->setCursor(Qt::PointingHandCursor);
        viewBy->setStyleSheet(QStringLiteral(
            "QToolButton { border: 1px solid transparent; border-radius: 2px; color: #0066cc; background: transparent; padding: 1px 14px 1px 3px; }"
            "QToolButton:hover { border-color: #b8d6fb; background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fafbfd, stop:1 #ebf3fd); }"
            "QToolButton::menu-indicator { image: none; width: 0px; }"));
        auto *arrowLabel = new QLabel(viewBy);
        {
            QPixmap arrow(7, 4);
            arrow.fill(Qt::transparent);
            QPainter p(&arrow);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x33, 0x66, 0xcc));
            p.drawPolygon(QPolygonF({QPointF(0, 0), QPointF(7, 0), QPointF(3.5, 4)}));
            p.end();
            arrowLabel->setPixmap(arrow);
        }
        auto *menu = new QMenu(viewBy);
        auto *group = new QActionGroup(menu);
        for (int view : {CategoryView, LargeIcons, SmallIcons}) {
            QAction *a = menu->addAction(viewName(view), this, [this, view] {
                setView(view);
            });
            a->setCheckable(true);
            a->setChecked(view == m_view);
            group->addAction(a);
            if (view == CategoryView) {
                menu->addSeparator();
            }
        }
        viewBy->setMenu(menu);
        top->addWidget(viewBy);
        v->addLayout(top);
        QTimer::singleShot(0, arrowLabel, [viewBy, arrowLabel] {
            arrowLabel->move(viewBy->width() - 11, (viewBy->height() - 4) / 2 + 1);
        });

        if (m_view == CategoryView) {
            v->addSpacing(28);
            auto *cols = new QHBoxLayout;
            cols->setSpacing(0); // two 350 px columns (Windows 7)
            for (const auto *col : {&leftColumn(), &rightColumn()}) {
                auto *cv = new QVBoxLayout;
                cv->setSpacing(18);
                for (const CategoryInfo &c : *col) {
                    cv->addWidget(categoryBlock(c));
                }
                cv->addStretch(1);
                cols->addLayout(cv, 1);
            }
            v->addLayout(cols);
        } else {
            // thin line under the title, then the items in columns
            auto *line = new QFrame;
            line->setFixedHeight(1);
            line->setStyleSheet(QStringLiteral("background: #e6ebf3;"));
            v->addSpacing(12);
            v->addWidget(line);
            v->addSpacing(16);
            QList<Item> items = allItems();
            if (!m_filter.isEmpty()) {
                items.erase(std::remove_if(items.begin(), items.end(), [this](const Item &i) {
                                return !i.name.contains(m_filter, Qt::CaseInsensitive);
                            }),
                            items.end());
            }
            const bool large = (m_view == LargeIcons);
            // the items flow left to right and wrap like an Explorer icon view
            const int columns = itemColumns();
            m_columns = columns;
            auto *grid = new QGridLayout;
            grid->setHorizontalSpacing(large ? 24 : 16);
            grid->setVerticalSpacing(large ? 10 : 3);
            for (int k = 0; k < items.size(); ++k) {
                const Item &it = items.at(k);
                QIcon icon = large ? QIcon::fromTheme(it.bigIcon, QIcon::fromTheme(it.icon)) : QIcon::fromTheme(it.icon);
                grid->addWidget(new ItemLink(icon, large ? 32 : 16, it.name, it.open), k / columns, k % columns);
            }
            for (int c = 0; c < columns; ++c) {
                grid->setColumnStretch(c, 1);
            }
            v->addLayout(grid);
            if (items.isEmpty()) {
                v->addWidget(grey(tr7("No items match your search.")));
            }
        }
        v->addStretch(1);
        m_scroll->setWidget(page);
    }

    /** Left: category list on a light blue pane. Right: the sections of the category */
    QWidget *buildCategoryPage(const CategoryPage &page)
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

        auto *side = new QWidget;
        side->setObjectName(QStringLiteral("win7side"));
        side->setAttribute(Qt::WA_StyledBackground, true);
        side->setFixedWidth(200);
        auto *sv = new QVBoxLayout(side);
        sv->setContentsMargins(6, 16, 10, 14);
        sv->setSpacing(7);
        const QString sideColor = QStringLiteral("#151c55");
        // one row = [bullet column][text]; the bullet is only shown for the current category
        auto addRow = [sv](QWidget *text, bool current) {
            auto *row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(0);
            // Win7 draws a small antialiased 6x5 dot in the text colour, centred on the cap height
            struct Dot : QWidget {
                bool on;
                explicit Dot(bool c) : on(c) { setFixedSize(23, 16); }
                void paintEvent(QPaintEvent *) override
                {
                    if (!on) {
                        return;
                    }
                    QPainter p(this);
                    p.setRenderHint(QPainter::Antialiasing);
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor(0x15, 0x1c, 0x55));
                    p.drawEllipse(QRectF(5.6, 6.4, 5.4, 5.4));
                }
            };
            auto *dot = new Dot(current);
            row->addWidget(dot, 0, Qt::AlignTop);
            row->addWidget(text, 1);
            sv->addLayout(row);
        };
        addRow(new Link(tr7("Control Panel Home"), [this] {
                   setView(CategoryView);
               }, sideColor),
               false);
        sv->addSpacing(9);
        for (const auto *col : {&leftColumn(), &rightColumn()}) {
            for (const CategoryInfo &c : *col) {
                if (c.title == page.title) {
                    auto *cur = new QLabel(c.title);
                    cur->setWordWrap(true);
                    cur->setStyleSheet(QStringLiteral("color: #151c55; font-weight: bold;"));
                    addRow(cur, true);
                } else {
                    addRow(new Link(c.title, c.open, sideColor), false);
                }
            }
        }
        sv->addStretch(1);
        h->addWidget(side);

        auto *main = new QWidget;
        auto *mv = new QVBoxLayout(main);
        mv->setContentsMargins(10, 10, 26, 20);
        mv->setSpacing(4);
        for (const PageSection &sec : page.sections) {
            mv->addWidget(pageSection(sec));
        }
        mv->addStretch(1);
        h->addWidget(main, 1);
        return root;
    }

    QScrollArea *m_scroll;
    /** as many columns as fit, like the Large/Small icons views of Windows 7 */
    int itemColumns() const
    {
        const int w = m_scroll->viewport()->width() - 80;
        return m_view == LargeIcons ? qBound(2, w / 250, 6) : qBound(2, w / 220, 7);
    }

    void resizeEvent(QResizeEvent *e) override
    {
        QMainWindow::resizeEvent(e);
        if ((m_view == LargeIcons || m_view == SmallIcons) && m_columns && itemColumns() != m_columns) {
            QTimer::singleShot(0, this, [this] {
                rebuild();
            });
        }
    }

    int m_columns = 0;
    QToolBar *m_commandBar;
    Win7Net::ConnectionsPage *m_connections = nullptr;
    Win7Dev::DevicesPage *m_devices = nullptr;
    Win7Programs::ProgramsPage *m_programs = nullptr;
    Win7Fonts::FontsPage *m_fonts = nullptr;
    Win7Programs::AssocPage *m_assoc = nullptr;
    Win7Header m_header;
    int m_view = CategoryView;
    QList<int> m_history;
    QList<int> m_future;
    QString m_filter;
};
}

#include "win7kwin.h"
#include <QDBusMessage>
#include <QDBusConnection>
#include <KConfigGroup>
#include <KConfig>
#include "win7windowclass.h"

int runKRunner(); // sevenrunner.cpp

int main(int argc, char **argv)
{
    // Disk Management: a WM_CLASS no desktop file is named after, so KWin keeps the snap-in icon in the title bar
    // while the taskbar maps it to seven-diskmgmt.desktop (taskmanagerrulesrc) and shows the mmc.exe toolbox
    for (int i = 1; i < argc; ++i) {
        // the Action Center flag in the tray asks for the messages, no window
        if (qstrcmp(argv[i], "--action-center-status") == 0) {
            QCoreApplication core(argc, argv);
            fputs(Win7Action::statusJson().constData(), stdout);
            fputs("\n", stdout);
            return 0;
        }
        if (qstrcmp(argv[i], "--screensaver-settings") == 0 || qstrcmp(argv[i], "--appearance-advanced") == 0) {
            qputenv("RESOURCE_NAME", "seven-desk"); // desk.cpl's own taskbar button and icon
        }
        if (qstrcmp(argv[i], "--notepad") == 0) {
            qputenv("RESOURCE_NAME", "seven-notepad"); // its own taskbar button and icon
        }
        if (qstrcmp(argv[i], "--diskmgmt") == 0) {
            qputenv("RESOURCE_NAME", "win7diskmgmtconsole");
        }
        if (qstrcmp(argv[i], "--backup-run") == 0) {
            qputenv("QT_QPA_PLATFORM", "offscreen"); // runs from the systemd timer, without windows
        }
        if (qstrcmp(argv[i], "--eventvwr") == 0) {
            qputenv("RESOURCE_NAME", "win7eventvwrconsole");
        }
        if (qstrcmp(argv[i], "--compmgmt") == 0) {
            qputenv("RESOURCE_NAME", "win7compmgmtconsole");
        }
    }
    QApplication app(argc, argv);
    Win7I18n::install(&app);
    // Windows 7 screen savers: one running full screen, or the resident part that starts them after the wait
    {
        const int saverArg = app.arguments().indexOf(QStringLiteral("--screensaver"));
        if (saverArg > 0) {
            app.setQuitOnLastWindowClosed(true);
            new Win7ScreenSaver::FullScreen(app.arguments().value(saverArg + 1, QStringLiteral("blank")),
                                            app.arguments().contains(QStringLiteral("--lock")) || (app.arguments().size() <= saverArg + 2
                                                                                                   && Win7ScreenSaver::config().readEntry("Lock", false)
                                                                                                   && !app.arguments().contains(QStringLiteral("--preview"))));
            return app.exec();
        }
        if (app.arguments().contains(QStringLiteral("--screensaver-daemon"))) {
            // one at a time
            QDBusConnection bus = QDBusConnection::sessionBus();
            if (!bus.registerService(QStringLiteral("org.sevenplasma.ScreenSaverDaemon"))) {
                return 0;
            }
            app.setQuitOnLastWindowClosed(false);
            new Win7ScreenSaver::Daemon;
            return app.exec();
        }
        if (app.arguments().contains(QStringLiteral("--appearance-advanced"))) {
            app.setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-desk")));
            app.setDesktopFileName(QStringLiteral("seven-desk")); // its own taskbar button
            Win7WindowColor::AdvancedDialog dlg;
            dlg.show();
            QObject::connect(&dlg, &QDialog::finished, &app, &QApplication::quit);
            return app.exec();
        }
        if (app.arguments().contains(QStringLiteral("--screensaver-settings"))) {
            app.setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-desk")));
            app.setDesktopFileName(QStringLiteral("seven-desk")); // its own taskbar button
            Win7ScreenSaver::SettingsDialog dlg;
            dlg.show();
            QObject::connect(&dlg, &QDialog::finished, &app, &QApplication::quit);
            return app.exec();
        }
    }
    // test helper: WIN7_GRAB=<prefix> saves the active window every 1.5 s (offscreen too); WIN7_GRAB_CLICK="Далі|Далі"
    // presses those buttons one by one in between, so pages can be checked without touching the screen
    if (!qEnvironmentVariableIsEmpty("WIN7_GRAB")) {
        auto *timer = new QTimer(&app);
        static int shot = 0;
        static QStringList clicks = qEnvironmentVariable("WIN7_GRAB_CLICK").split(QLatin1Char('|'), Qt::SkipEmptyParts);
        QObject::connect(timer, &QTimer::timeout, &app, [] {
            QWidget *w = QApplication::activeModalWidget();
            if (!w) {
                for (QWidget *t : QApplication::topLevelWidgets()) {
                    if (t->isVisible() && !qobject_cast<QMenu *>(t) && t->width() > 200) {
                        w = t;
                    }
                }
            }
            if (!w) {
                return;
            }
            w->grab().save(qEnvironmentVariable("WIN7_GRAB") + QStringLiteral("%1.png").arg(shot++, 2, 10, QLatin1Char('0')));
            if (clicks.isEmpty()) {
                QApplication::exit(0);
                _exit(0);
            }
            const QString want = clicks.takeFirst();
            if (want.startsWith(QLatin1String("radio:"))) {
                for (QRadioButton *b : w->findChildren<QRadioButton *>()) {
                    if (b->text() == want.mid(6) && b->isVisible()) {
                        b->click();
                    }
                }
                return;
            }
            if (want.startsWith(QLatin1String("item:"))) {
                for (QTreeWidget *t : w->findChildren<QTreeWidget *>()) {
                    const auto found = t->findItems(want.mid(5), Qt::MatchExactly | Qt::MatchRecursive);
                    if (!found.isEmpty()) {
                        t->setCurrentItem(found.first());
                        return;
                    }
                }
                return;
            }
            if (want.startsWith(QLatin1String("tab:"))) {
                if (auto *t = w->findChild<QTabWidget *>()) {
                    t->setCurrentIndex(want.mid(4).toInt());
                }
                return;
            }
            if (want.startsWith(QLatin1String("size:"))) {
                w->resize(want.mid(5).section(QLatin1Char('x'), 0, 0).toInt(), want.mid(5).section(QLatin1Char('x'), 1, 1).toInt());
                return;
            }
            if (want.startsWith(QLatin1String("view:")) && g_openView) {
                g_openView(want.mid(5).toInt());
                return;
            }
            if (want.startsWith(QLatin1String("link:"))) {
                for (QLabel *l : w->findChildren<QLabel *>()) {
                    if (l->text() == want.mid(5) && l->isVisible()) {
                        QTimer::singleShot(0, l, [l] {
                            const QPointF c = QRectF(l->rect()).center();
                            QMouseEvent press(QEvent::MouseButtonPress, c, l->mapToGlobal(c), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                            QMouseEvent release(QEvent::MouseButtonRelease, c, l->mapToGlobal(c), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                            QCoreApplication::sendEvent(l, &press);
                            QCoreApplication::sendEvent(l, &release);
                        });
                        return;
                    }
                }
                return;
            }
            if (want.startsWith(QLatin1String("list:"))) {
                for (QListWidget *l : w->findChildren<QListWidget *>()) {
                    if (l->isVisible() && l->count() > want.mid(5).toInt()) {
                        l->setCurrentRow(want.mid(5).toInt());
                    }
                }
                return;
            }
            if (want.startsWith(QLatin1String("row:"))) {
                for (QTreeWidget *t : w->findChildren<QTreeWidget *>()) {
                    if (t->isVisible() && t->topLevelItemCount() > want.mid(4).toInt()) {
                        t->setCurrentItem(t->topLevelItem(want.mid(4).toInt()));
                    }
                }
                return;
            }
            for (QAbstractButton *b : w->findChildren<QAbstractButton *>()) {
                if (b->text().remove(QLatin1Char('&')) == want && b->isVisible() && b->isEnabled()) {
                    QTimer::singleShot(0, b, &QAbstractButton::click);
                    return;
                }
            }
        });
        timer->start(1500);
    }
    app.setApplicationName(QStringLiteral("seven-control"));
    app.setApplicationDisplayName(tr7("Control Panel"));
    app.setDesktopFileName(QStringLiteral("seven-control"));
    Aero::registerStylesheet(&app);
    app.setStyle(new Win7ButtonStyle(QApplication::style())); // on top of the Aero style: shields on buttons
    Win7TabEdgeFix::install(&app);

    // tray menu "Troubleshoot problems"
    if (app.arguments().contains(QStringLiteral("--net-troubleshoot"))) {
        Win7Net::diagnoseNetwork(nullptr);
        return 0;
    }
    // desktop shortcuts of network connections open only the dialog
    const int netConnect = app.arguments().indexOf(QStringLiteral("--net-connect"));
    const int netStatus = app.arguments().indexOf(QStringLiteral("--net-status"));
    if (netConnect > 0 || netStatus > 0) {
        app.setApplicationDisplayName(QString()); // "Стан: …" without " — Панель керування", like Windows 7
        const auto tile = Win7Net::findTile(app.arguments().value((netConnect > 0 ? netConnect : netStatus) + 1));
        if (!tile) {
            QMessageBox::warning(nullptr, tr7("Network Connections"), tr7("No connections found."));
            return 1;
        }
        if (netConnect > 0 && !tile->active) {
            Win7Net::connectDialog(nullptr, *tile, [] {});
            return 0;
        }
        if (!tile->active) {
            return 0;
        }
        auto *status = new Win7Net::StatusDialog(nullptr, *tile, [] {});
        status->show();
        return app.exec();
    }

    // stand-alone Windows 7 dialogs: plain titles, without " — Панель керування"
    if (app.arguments().contains(QStringLiteral("--display-switch"))) { // Win+P
        return Win7DisplaySwitch::run();
    }
    if (app.arguments().contains(QStringLiteral("--autoplay-ask"))) { // test helper: the AutoPlay window for a USB drive
        Win7Autoplay::Media m;
        m.device = QStringLiteral("/dev/sdx1");
        m.label = tr7("Removable Disk");
        m.type = QStringLiteral("mixed");
        Win7Autoplay::askDialog(m);
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--soundfx-rebuild"))) {
        Win7SoundFx::rebuild(); // regenerate the Enhancements filters from ~/.config/seven-soundfxrc
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--balloon-test"))) { // test helper: the USB speed balloon
        Win7SafelyRemove::balloon(tr7("This device can perform faster"),
                                  tr7("This USB device can perform faster if you connect it to a Hi-Speed USB 2.0 port.\nFor a list of available ports, click here."));
        QTimer::singleShot(4000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--autoplay-watch"))) {
        return Win7Autoplay::watch(app);
    }
    for (const char *flag : {"--sysprops", "--visualfx", "--sndvol", "--mouse", "--sound", "--tts", "--datetime", "--set-datetime", "--timezone", "--region", "--text-services", "--taskbar-props", "--new-toolbar"}) {
        if (app.arguments().contains(QLatin1String(flag))) {
            app.setApplicationDisplayName(QString());
        }
    }
    // Date and Time, Region and Language (each with its own desktop file for the title bar icon)
    auto argAfter = [&](const QString &flag) {
        return app.arguments().value(app.arguments().indexOf(flag) + 1).toInt();
    };
    if (app.arguments().contains(QStringLiteral("--datetime"))) {
        app.setDesktopFileName(QStringLiteral("seven-datetime"));
        auto *dlg = new Win7Time::DateTimeDialog(argAfter(QStringLiteral("--datetime")));
        dlg->openRegion = [] {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--region")});
        };
        dlg->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--set-datetime"))) {
        app.setDesktopFileName(QStringLiteral("seven-datetime"));
        Win7Time::DateTimeSettings dlg(nullptr);
        dlg.openCalendarSettings = [] {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--region")});
        };
        dlg.exec();
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--timezone"))) {
        app.setDesktopFileName(QStringLiteral("seven-datetime"));
        Win7Time::TimeZoneSettings dlg(nullptr);
        dlg.exec();
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--play-sound"))) {
        Win7Sound::playSound(app.arguments().value(app.arguments().indexOf(QStringLiteral("--play-sound")) + 1));
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--sound"))) {
        app.setDesktopFileName(QStringLiteral("seven-sound"));
        auto *dlg = new Win7Sound::SoundDialog(argAfter(QStringLiteral("--sound")));
        dlg->openSpeech = [] {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--speech")});
        };
        dlg->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--winsat"))) {
        // like "winsat formal": rate the computer and print the scores
        const bool ok = Win7Perf::rate(nullptr);
        QSettings &w = Win7Perf::store();
        for (const char *k : {"Processor", "Memory", "Graphics", "Gaming", "Disk"}) {
            printf("%s: %.1f\n", k, w.value(QStringLiteral("Scores/") + QLatin1String(k)).toDouble());
        }
        return ok ? 0 : 1;
    }
    if (app.arguments().contains(QStringLiteral("--sysprops"))) {
        app.setDesktopFileName(QStringLiteral("seven-system"));
        (new Win7SysProps::SystemProperties(argAfter(QStringLiteral("--sysprops"))))->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--sndvol"))) {
        app.setDesktopFileName(QStringLiteral("seven-sndvol"));
        (new Win7SndVol::Mixer)->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--visualfx-apply"))) {
        return Win7VisualFx::applyAtLogin();
    }
    if (app.arguments().contains(QStringLiteral("--visualfx"))) {
        app.setDesktopFileName(QStringLiteral("seven-system"));
        (new Win7VisualFx::PerformanceOptions)->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--mouse"))) {
        app.setDesktopFileName(QStringLiteral("seven-mouse"));
        (new Win7Mouse::MouseDialog(argAfter(QStringLiteral("--mouse"))))->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--tts"))) {
        app.setDesktopFileName(QStringLiteral("seven-sound"));
        Win7Sound::SpeechProperties dlg;
        dlg.exec();
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--region"))) {
        app.setDesktopFileName(QStringLiteral("seven-region"));
        (new Win7Region::RegionDialog(argAfter(QStringLiteral("--region"))))->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--text-services"))) {
        app.setDesktopFileName(QStringLiteral("seven-textservices"));
        Win7Region::TextServicesDialog dlg(argAfter(QStringLiteral("--text-services")));
        dlg.exec();
        return 0;
    }

    // taskbar menu > Toolbars
    const int toolbarArg = app.arguments().indexOf(QStringLiteral("--toolbar"));
    if (toolbarArg > 0) {
        Win7Taskbar::toggleToolbar(app.arguments().value(toolbarArg + 1));
        return 0;
    }
    const int removeArg = app.arguments().indexOf(QStringLiteral("--toolbar-remove"));
    if (removeArg > 0) {
        Win7Taskbar::removeToolbar(app.arguments().value(removeArg + 1).toInt());
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--new-toolbar"))) {
        Win7Taskbar::newToolbar();
        return 0;
    }
    const int propsArg = app.arguments().indexOf(QStringLiteral("--taskbar-props"));
    if (propsArg > 0) { // right click on the taskbar / Start button > Properties
        app.setDesktopFileName(QStringLiteral("seven-taskbar-props")); // the title bar takes its icon from here
        Win7Taskbar::showProperties(app.arguments().value(propsArg + 1) == QLatin1String("start") ? 1 : 0);
        return app.exec();
    }

    ControlPanel w(app.arguments().contains(QStringLiteral("--all")));
    const int pageArg = app.arguments().indexOf(QStringLiteral("--page"));
    if (pageArg > 0 && g_openCategoryPage) {
        g_openCategoryPage(app.arguments().value(pageArg + 1).toInt());
    }
    // "--view <n>": open one applet page directly (Explorer's "Uninstall or change a program" opens 30)
    const int viewArg = app.arguments().indexOf(QStringLiteral("--view"));
    if (viewArg > 0 && g_openView) {
        g_openView(app.arguments().value(viewArg + 1).toInt());
    }
    if (app.arguments().contains(QStringLiteral("--desktop-icons"))) {
        Win7Display::desktopIconsDialog(nullptr);
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--speech")) && g_openView) {
        g_openView(97);
    }
    const int trayArg = app.arguments().indexOf(QStringLiteral("--tray-icons"));
    if (trayArg > 0 && g_openView) {
        g_launchView = app.arguments().value(trayArg + 1) == QLatin1String("system") ? Win7Tray::SystemView : Win7Tray::IconsView;
        g_openView(g_launchView);
    }
    if (app.arguments().contains(QStringLiteral("--games")) && g_openView) {
        app.setDesktopFileName(QStringLiteral("seven-games"));
        g_openView(Win7Games::GamesView);
    }
    if (app.arguments().contains(QStringLiteral("--perf")) && g_openView) {
        g_openView(Win7Perf::PerfView);
    }
    if (app.arguments().contains(QStringLiteral("--devices")) && g_openView) {
        g_openView(Win7Dev::DevicesView);
    }
    if (app.arguments().contains(QStringLiteral("--autoplay")) && g_openView) {
        g_launchView = Win7Autoplay::AutoplayView;
        g_openView(g_launchView);
    }
    if (app.arguments().contains(QStringLiteral("--add-printer"))) {
        app.setDesktopFileName(QStringLiteral("seven-add-printer"));
        auto *wiz = new Win7Printer::AddPrinterWizard(nullptr, {});
        QObject::connect(wiz, &QObject::destroyed, &app, &QCoreApplication::quit);
        wiz->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--krunner"))) {
        app.setQuitOnLastWindowClosed(false);
        return runKRunner();
    }
    const int cleanArg = app.arguments().indexOf(QStringLiteral("--cleanmgr"));
    if (cleanArg > 0) {
        app.setDesktopFileName(QStringLiteral("seven-cleanmgr"));
        auto *dlg = new Win7Cleanup::CleanupDialog(app.arguments().value(cleanArg + 1) == QLatin1String("system"));
        if (!dlg->prepare()) {
            return 0;
        }
        QObject::connect(dlg, &QObject::destroyed, &app, &QCoreApplication::quit);
        dlg->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--diskmgmt"))) {
        // Windows 7: the title bar shows the snap-in icon, the taskbar the mmc.exe toolbox. KWin would take the
        // title bar icon from a desktop file too, so the taskbar finds seven-diskmgmt.desktop by window class instead
        app.setDesktopFileName(QStringLiteral("win7diskmgmtconsole")); // no such desktop file: KWin keeps our window icon
        app.setApplicationName(QStringLiteral("Win7DiskMgmtConsole"));
        auto *w = new Win7Disk::DiskManagement;
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--compmgmt"))) {
        // like the other MMC consoles: the snap-in icon in the title bar, the console in the taskbar
        app.setDesktopFileName(QStringLiteral("win7compmgmtconsole"));
        app.setApplicationName(QStringLiteral("Win7CompMgmtConsole"));
        app.setApplicationDisplayName(QString());
        auto *cm = new Win7Compmgmt::ComputerManagement;
        cm->window()->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--eventvwr"))) {
        // same as Disk Management: the snap-in icon in the title bar, the mmc.exe toolbox in the taskbar
        app.setDesktopFileName(QStringLiteral("win7eventvwrconsole"));
        app.setApplicationName(QStringLiteral("Win7EventVwrConsole"));
        app.setApplicationDisplayName(QString());
        auto *w = new Win7Event::EventViewer;
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--wf-newrule"))) { // test helper: the new rule wizard alone
        Win7Wf::RuleWizard w(nullptr, true);
        w.exec();
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--wf"))) {
        app.setDesktopFileName(QStringLiteral("seven-wf"));
        app.setApplicationDisplayName(QString());
        auto *w = new Win7Wf::FirewallConsole;
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--backup-run"))) {
        return Win7Backup::runBackup();
    }
    if (app.arguments().contains(QStringLiteral("--restore-files"))) {
        app.setDesktopFileName(QStringLiteral("seven-control"));
        const Win7Backup::Settings bs = Win7Backup::load();
        const QString root = bs.configured ? Win7Backup::destinationRoot(bs) : QString();
        Win7Backup::RestoreWizard w(nullptr, root.isEmpty() || Win7Backup::setsIn(root).isEmpty(), root);
        w.exec();
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--sysimage"))) {
        Win7Backup::createSystemImage(nullptr);
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--rstrui"))) {
        if (Win7Backup::ensureTimeshift(nullptr)) {
            Win7Backup::SystemRestore(nullptr).exec();
        }
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--memtest"))) {
        app.setDesktopFileName(QStringLiteral("seven-memtest"));
        return Win7Memtest::run();
    }
    if (app.arguments().contains(QStringLiteral("--osk"))) {
        app.setDesktopFileName(QStringLiteral("seven-osk"));
        if (Win7KWin::wayland()) {
            // Wayland: a click must not take the focus from the window being typed into, and Windows 7 keeps
            // the keyboard above the others; clients cannot ask for either, so a KWin rule for our app id does
            KConfig rules(QStringLiteral("kwinrulesrc"));
            KConfigGroup general = rules.group(QStringLiteral("General"));
            QStringList ids = general.readEntry("rules", QString()).split(QLatin1Char(','), Qt::SkipEmptyParts);
            const QString id = QStringLiteral("seven-osk");
            if (!ids.contains(id)) {
                KConfigGroup r = rules.group(id);
                r.writeEntry("Description", QStringLiteral("On-Screen Keyboard (SevenPlasma)"));
                r.writeEntry("wmclass", id);
                r.writeEntry("wmclassmatch", 1);
                r.writeEntry("acceptfocus", false);
                r.writeEntry("acceptfocusrule", 2);
                r.writeEntry("above", true);
                r.writeEntry("aboverule", 2);
                ids << id;
                general.writeEntry("rules", ids.join(QLatin1Char(',')));
                general.writeEntry("count", ids.size());
                rules.sync();
                QDBusConnection::sessionBus().call(QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                                                                                  QStringLiteral("org.kde.KWin"), QStringLiteral("reconfigure")));
            }
        }
        auto *osk = new Win7Access::KeyboardWindow;
        osk->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--magnifier"))) {
        app.setDesktopFileName(QStringLiteral("seven-magnifier"));
        auto *m = new Win7Access::MagnifierWindow;
        m->show();
        Win7KWin::moveTo(m, QGuiApplication::primaryScreen()->availableGeometry().topLeft() + QPoint(60, 40));
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--narrator"))) {
        app.setDesktopFileName(QStringLiteral("seven-narrator"));
        (new Win7Access::NarratorWindow)->show();
        return app.exec();
    }
    const int keyboardArg = app.arguments().indexOf(QStringLiteral("--keyboard"));
    if (keyboardArg > 0) {
        app.setDesktopFileName(QStringLiteral("seven-keyboard"));
        (new Win7Access::KeyboardDialog(app.arguments().value(keyboardArg + 1).toInt()))->show();
        return app.exec();
    }
    const int fontviewArg = app.arguments().indexOf(QStringLiteral("--fontview"));
    if (fontviewArg > 0) {
        app.setDesktopFileName(QStringLiteral("seven-fontview"));
        const QString file = QFileInfo(app.arguments().value(fontviewArg + 1)).absoluteFilePath();
        auto *w = new Win7Fonts::FontViewer(file, app.arguments().value(fontviewArg + 2).toInt());
        w->show();
        if (app.arguments().contains(QStringLiteral("--print"))) {
            QTimer::singleShot(0, w, [w] {
                w->printSheet();
                w->close();
            });
        }
        return app.exec();
    }
    const int installArg = app.arguments().indexOf(QStringLiteral("--font-install"));
    if (installArg > 0) {
        QStringList files;
        for (const QString &f : app.arguments().mid(installArg + 1)) {
            files << QFileInfo(f).absoluteFilePath();
        }
        Win7Fonts::installFonts(nullptr, files);
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--inetopts"))) {
        app.setDesktopFileName(QStringLiteral("seven-control"));
        auto *dlg = new Win7Inet::InternetOptions;
        QObject::connect(dlg, &QObject::destroyed, &app, &QCoreApplication::quit);
        dlg->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--colormgmt"))) {
        app.setDesktopFileName(QStringLiteral("seven-control"));
        auto *dlg = new Win7Color::ColorManagement;
        QObject::connect(dlg, &QObject::destroyed, &app, &QCoreApplication::quit);
        dlg->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--run"))) {
        app.setDesktopFileName(QStringLiteral("seven-run"));
        auto *dlg = new Win7Run::RunDialog;
        QObject::connect(dlg, &QObject::destroyed, &app, &QCoreApplication::quit);
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
        return app.exec();
    }
    const int notepadArg = app.arguments().indexOf(QStringLiteral("--notepad"));
    if (notepadArg > 0) {
        app.setDesktopFileName(QStringLiteral("seven-notepad"));
        app.setApplicationDisplayName(QString());
        QString file = app.arguments().value(notepadArg + 1);
        if (file.startsWith(QLatin1String("file://"))) {
            file = QUrl(file).toLocalFile();
        }
        auto *w = new Win7Notepad::Notepad(file.startsWith(QLatin1String("--")) ? QString() : file);
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
        return app.exec();
    }
    const int propertiesArg = app.arguments().indexOf(QStringLiteral("--properties"));
    if (propertiesArg > 0) {
        // seven-properties.desktop has no icon: the taskbar and the title bar show the item's icon, like Windows 7
        app.setDesktopFileName(QStringLiteral("seven-properties"));
        QStringList paths;
        for (const QString &f : app.arguments().mid(propertiesArg + 1)) {
            if (!f.startsWith(QLatin1String("--")) && QFileInfo::exists(f)) {
                paths << QFileInfo(f).absoluteFilePath();
            }
        }
        if (paths.isEmpty()) {
            return 1;
        }
        // the Recycle Bin on the desktop: its own Properties, like Windows 7
        if (paths.size() == 1 && Win7Trash::isTrashLink(paths.first())) {
            app.setApplicationDisplayName(QString());
            auto *bin = new Win7Trash::RecycleBinDialog;
            QObject::connect(bin, &QObject::destroyed, &app, &QCoreApplication::quit);
            bin->show();
            return app.exec();
        }
        auto *dlg = new Win7Props::PropertiesDialog(paths);
        QObject::connect(dlg, &QObject::destroyed, &app, &QCoreApplication::quit);
        dlg->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--portal"))) { // xdg-desktop-portal backend: "Open with" for all programs
        app.setDesktopFileName(QStringLiteral("seven-control"));
        app.setQuitOnLastWindowClosed(false);
        if (!Win7Portal::start()) {
            return 1;
        }
        return app.exec();
    }
    const int openWithArg = app.arguments().indexOf(QStringLiteral("--open-with"));
    if (openWithArg > 0) {                                 // Windows 7 "Open with" for local files (called by kio-win7)
        app.setDesktopFileName(QStringLiteral("seven-control"));
        QStringList files;
        for (const QString &a : app.arguments().mid(openWithArg + 1)) if (!a.startsWith(QLatin1String("--"))) files << QFileInfo(a).absoluteFilePath();
        if (files.isEmpty()) return 1;
        auto *dlg = new Win7OpenWith::Dialog(files);
        QObject::connect(dlg, &QObject::destroyed, &app, &QCoreApplication::quit);
        dlg->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--netplwiz"))) {       // netplwiz / control userpasswords2
        app.setDesktopFileName(QStringLiteral("seven-control"));
        Win7Users::NetplwizDialog d;
        // a Windows 7 dialog frame: no icon, no minimise / maximise (smodrc exception ^seven-dialog)
        return Win7WindowClass::exec(&d, "seven-dialog", "seven-control") == QDialog::Accepted ? 0 : 1;
    }
    const int browseArg = app.arguments().indexOf(QStringLiteral("--browse-folder"));
    if (browseArg > 0) {                                   // Windows 7 "Browse For Folder"; prints the chosen folder
        app.setDesktopFileName(QStringLiteral("seven-control"));
        const QString start = app.arguments().value(browseArg + 1);
        const QString dir = Win7BrowseFolder::getFolder(nullptr, tr7("Select a folder."), start.startsWith(QLatin1String("--")) ? QString() : start);
        if (!dir.isEmpty()) printf("%s\n", dir.toLocal8Bit().constData());
        return dir.isEmpty() ? 1 : 0;
    }
    if (app.arguments().contains(QStringLiteral("--trash-props"))) {
        app.setDesktopFileName(QStringLiteral("seven-properties"));
        app.setApplicationDisplayName(QString());
        auto *bin = new Win7Trash::RecycleBinDialog;
        QObject::connect(bin, &QObject::destroyed, &app, &QCoreApplication::quit);
        bin->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--add-device"))) {
        app.setDesktopFileName(QStringLiteral("seven-control"));
        app.setQuitOnLastWindowClosed(true);
        Win7Dev::addDeviceWizard(nullptr, {});
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--indexing"))) {
        app.setDesktopFileName(QStringLiteral("seven-control"));
        auto *dlg = new Win7Indexing::IndexingDialog;
        QObject::connect(dlg, &QObject::destroyed, &app, &QCoreApplication::quit);
        dlg->show();
        return app.exec();
    }
    const int folderOptsArg = app.arguments().indexOf(QStringLiteral("--folder-options"));
    if (folderOptsArg > 0) {
        app.setDesktopFileName(QStringLiteral("seven-control"));
        const QString folder = app.arguments().value(folderOptsArg + 1);
        auto *dlg = new Win7FolderOpts::FolderOptionsDialog(folder.startsWith(QLatin1String("--")) ? QString() : folder);
        const int tabArg = app.arguments().indexOf(QStringLiteral("--tab"));
        if (tabArg > 0) {
            dlg->findChild<QTabWidget *>()->setCurrentIndex(app.arguments().value(tabArg + 1).toInt());
        }
        QObject::connect(dlg, &QObject::destroyed, &app, &QCoreApplication::quit);
        dlg->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--charmap"))) {
        app.setDesktopFileName(QStringLiteral("seven-charmap"));
        (new Win7Fonts::CharMap)->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--cttune"))) {
        app.setDesktopFileName(QStringLiteral("seven-cttune"));
        (new Win7Fonts::ClearTypeTuner)->show();
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--features"))) {
        app.setDesktopFileName(QStringLiteral("seven-features"));
        auto *dlg = new Win7Programs::FeaturesDialog;
        QObject::connect(dlg, &QObject::destroyed, &app, &QCoreApplication::quit);
        dlg->show();
        return app.exec();
    }
    for (const auto &[flag, v] : {std::pair{"--programs", int(Win7Programs::ProgramsView)}, std::pair{"--updates", int(Win7Programs::UpdatesView)},
                                  std::pair{"--default-programs", int(Win7Programs::DefaultsView)}, std::pair{"--set-defaults", int(Win7Programs::SetDefaultsView)},
                                  std::pair{"--associations", int(Win7Programs::AssocView)}, std::pair{"--users", int(Win7Users::HomeView)},
                                  std::pair{"--manage-users", int(Win7Users::ManageView)}, std::pair{"--backup", int(Win7Backup::BackupView)},
                                  std::pair{"--recovery", int(Win7Backup::RecoveryView)}}) {
        if (app.arguments().contains(QLatin1String(flag)) && g_openView) {
            g_openView(v);
        }
    }
    const int easeArg = app.arguments().indexOf(QStringLiteral("--ease"));
    if (easeArg > 0 && g_openView) {
        static const QHash<QString, int> pages{{QStringLiteral("see"), Win7Access::SeeView},         {QStringLiteral("nodisplay"), Win7Access::NoDisplayView},
                                               {QStringLiteral("noinput"), Win7Access::NoInputView}, {QStringLiteral("mouse"), Win7Access::MouseView},
                                               {QStringLiteral("keyboard"), Win7Access::KeyboardView}, {QStringLiteral("sounds"), Win7Access::SoundsView},
                                               {QStringLiteral("focus"), Win7Access::FocusView}};
        const QString page = app.arguments().value(easeArg + 1);
        g_launchView = pages.value(page, -1);
        g_openView(pages.value(page, Win7Access::CenterView));
    }
    if (app.arguments().contains(QStringLiteral("--firewall")) && g_openView) {
        g_openView(Win7Firewall::StatusView);
    }
    if (app.arguments().contains(QStringLiteral("--action-center")) && g_openView) {
        g_openView(Win7Action::CenterView);
    }
    if (app.arguments().contains(QStringLiteral("--windows-update")) && g_openView) {
        g_openView(Win7Update::UpdateView);
    }
    if (app.arguments().contains(QStringLiteral("--fonts")) && g_openView) {
        g_openView(Win7Fonts::FontsView);
    }
    if (app.arguments().contains(QStringLiteral("--personalize")) && g_openView) {
        g_openView(Win7Display::PersonalizeView);
    }
    if (app.arguments().contains(QStringLiteral("--display")) && g_openView) {
        g_openView(Win7Display::DisplayView);
    }
    if (app.arguments().contains(QStringLiteral("--resolution")) && g_openView) {
        g_openView(Win7Display::ResolutionView);
    }
    if (app.arguments().contains(QStringLiteral("--net-center")) && g_openView) {
        g_openView(Win7Net::CenterView);
    }
    if (app.arguments().contains(QStringLiteral("--net-sharing")) && g_openView) {
        g_openView(Win7Net::SharingView);
    }
    if (app.arguments().contains(QStringLiteral("--net-connections")) && g_openView) {
        g_openView(Win7Net::ConnectionsView);
    }
    const int powerArg = app.arguments().indexOf(QStringLiteral("--power"));
    if (powerArg > 0 && g_openView) {
        const QString what = app.arguments().value(powerArg + 1);
        g_openView(what == QLatin1String("buttons") ? Win7Power::ButtonsView
                       : what == QLatin1String("edit") ? Win7Power::EditView + Win7Power::planIndex(Win7Power::activePlan())
                                                       : Win7Power::PlansView);
    }
    // Windows 7 hosts the Control Panel in Explorer, whose frame shows neither an icon nor a title. Give this
    // window its own class so the decoration exception (smodrc, ^seven-cpanel) hides both, while the dialogs it
    // starts keep their captions; the taskbar maps it through _KDE_NET_WM_DESKTOP_FILE (X11) or seven-cpanel.desktop
    Win7WindowClass::show(&w, "seven-cpanel", "seven-control");
    return app.exec();
}
