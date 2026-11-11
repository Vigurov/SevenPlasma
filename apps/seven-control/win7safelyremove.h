#pragma once
#include "win7kwin.h"
#include "win7i18n.h"
/*
 * "Безпечне видалення пристрою та виймання носія" (hotplug.dll) in the notification area, run inside the AutoPlay
 * watcher (`seven-control --autoplay-watch`). Like Windows 7: the icon exists only while a removable drive is
 * connected and sits in the hidden icons (Passive status); a click opens the menu
 *   Відкрити пристрої та принтери
 *   ─────
 *   Вийняти <hardware name>
 *     - <volume> (sdb1)
 * Ejecting unmounts every volume of the drive and powers it off (udisksctl), then the balloon "Обладнання можна
 * видалити" appears; a busy volume gives "Проблема з вийманням ...". Never offers the drive Linux runs from.
 */
#include <KStatusNotifierItem>
#include <Solid/Block>
#include <Solid/Device>
#include <Solid/DeviceNotifier>
#include <Solid/OpticalDrive>
#include <Solid/StorageAccess>
#include <Solid/StorageDrive>
#include <Solid/StorageVolume>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWidget>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QMenu>
#include <QPointer>
#include <QProcess>
#include <QTimer>

namespace Win7SafelyRemove
{
struct RemovableDrive {
    Solid::Device drive;
    QString name; // hardware name ("USB Flash Disk")
    QString blockDevice; // /dev/sdb
    QList<Solid::Device> volumes;
};

inline QString hardwareName(const Solid::Device &d)
{
    QString n = d.product().trimmed();
    if (n.isEmpty()) {
        n = d.description();
    }
    return n;
}

/** removable / hot-pluggable drives, without the one that holds the running system */
inline QList<RemovableDrive> removableDrives()
{
    QList<RemovableDrive> out;
    QHash<QString, int> index;
    for (const Solid::Device &d : Solid::Device::listFromType(Solid::DeviceInterface::StorageDrive)) {
        const auto *sd = d.as<Solid::StorageDrive>();
        if (!sd || !(sd->isRemovable() || sd->isHotpluggable())) {
            continue;
        }
        RemovableDrive r;
        r.drive = d;
        r.name = hardwareName(d);
        if (const auto *b = d.as<Solid::Block>()) {
            r.blockDevice = b->device();
        }
        index.insert(d.udi(), out.size());
        out << r;
    }
    // volumes under each drive
    QSet<QString> system;
    for (const Solid::Device &v : Solid::Device::listFromType(Solid::DeviceInterface::StorageVolume)) {
        Solid::Device p = v.parent();
        while (p.isValid() && !index.contains(p.udi())) {
            p = p.parent();
        }
        if (!p.isValid()) {
            continue;
        }
        const auto *vol = v.as<Solid::StorageVolume>();
        if (vol && vol->isIgnored()) {
            continue;
        }
        if (const auto *acc = v.as<Solid::StorageAccess>()) {
            const QString fp = acc->filePath();
            if (fp == QLatin1String("/") || fp.startsWith(QLatin1String("/boot"))) {
                system.insert(p.udi());
            }
        }
        out[index.value(p.udi())].volumes << v;
    }
    QList<RemovableDrive> result;
    for (const auto &r : std::as_const(out)) {
        // the system disk (here an external USB SSD) is never offered; empty card readers neither
        if (system.contains(r.drive.udi())) {
            continue;
        }
        if (r.volumes.isEmpty() && !r.drive.is<Solid::OpticalDrive>()) {
            continue;
        }
        result << r;
    }
    return result;
}

inline QString volumeName(const Solid::Device &v)
{
    const auto *vol = v.as<Solid::StorageVolume>();
    QString label = vol ? vol->label() : QString();
    if (label.isEmpty()) {
        label = tr7("Removable Disk");
    }
    QString dev;
    if (const auto *b = v.as<Solid::Block>()) {
        dev = QFileInfo(b->device()).fileName();
    }
    return dev.isEmpty() ? label : QStringLiteral("%1 (%2)").arg(label, dev);
}

/** the Windows 7 notification-area balloon: rounded white→lavender box, (i) icon, blue title, wrench and ✕ at the
    top right, a tail down to the tray icon; click = action, closes itself after 10 s */
class Balloon : public QWidget
{
public:
    static constexpr int Tail = 18;

    Balloon(const QString &title, const QString &text, std::function<void()> clicked = {})
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
        , m_title(title)
        , m_text(text)
        , m_clicked(std::move(clicked))
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setMouseTracking(true);
        QFont tf = font();
        tf.setPixelSize(16);
        m_titleFont = tf;
        QFont bf = font();
        bf.setPixelSize(12);
        m_bodyFont = bf;
        // as wide as the title needs (Windows widens the balloon for it), at least 330 px
        const int w = qMax(330, QFontMetrics(m_titleFont).horizontalAdvance(m_title) + 31 + 60);
        const QFontMetrics bm(m_bodyFont);
        const QRect br = bm.boundingRect(QRect(0, 0, w - 40, 1000), Qt::TextWordWrap, m_text);
        m_box = QRect(4, 2, w, 33 + br.height() + 12);
        resize(w + 10, m_box.bottom() + Tail + 4);
        QTimer::singleShot(10000, this, &QWidget::close);
    }
    /** shows it with the tail tip at @p tip (a point on the tray) */
    void showAt(const QPoint &tip)
    {
        // the tail sits near the right end of the box, like on Windows 7
        m_tailX = width() - 48;
        QPoint pos(tip.x() - m_tailX, tip.y() - height());
        const QRect avail = QGuiApplication::screenAt(tip) ? QGuiApplication::screenAt(tip)->geometry() : QRect(0, 0, 1920, 1080);
        if (pos.x() + width() > avail.right()) {
            const int shift = pos.x() + width() - avail.right();
            pos.rx() -= shift;
            m_tailX += shift;
        }
        if (Win7KWin::wayland()) {
            // a parentless tooltip cannot be placed on Wayland: a tool window that KWin puts there, above all
            setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
            setWindowTitle(QStringLiteral("seven-balloon-%1").arg(quintptr(this), 0, 16));
            show();
            Win7KWin::forWindow(this, QStringLiteral("w.keepAbove = true; w.skipTaskbar = true; w.skipSwitcher = true; "
                                                     "w.frameGeometry = {x: %1, y: %2, width: w.frameGeometry.width, height: w.frameGeometry.height};")
                                          .arg(pos.x())
                                          .arg(pos.y()));
            return;
        }
        move(pos);
        show();
    }
    /** the tray position next to the ▲ of hidden icons (where Windows shows the promoted icon) */
    static QPoint trayTip()
    {
        const QScreen *sc = QGuiApplication::primaryScreen();
        const QRect g = sc->geometry(), a = sc->availableGeometry();
        const int bar = a.bottom() < g.bottom() ? a.bottom() + 1 : g.bottom() - 40;
        return QPoint(g.right() - 174, bar + 6);
    }

protected:
    QRect closeRect() const
    {
        return QRect(m_box.right() - 22, m_box.top() + 8, 14, 14);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        const QRectF b = QRectF(m_box).adjusted(0.5, 0.5, -0.5, -0.5);
        path.addRoundedRect(b, 4, 4);
        // tail: from the bottom edge down to the tip, its right side vertical (Windows 7)
        QPainterPath tail;
        tail.moveTo(m_tailX - 26, b.bottom() - 1);
        tail.lineTo(m_tailX, b.bottom() - 1);
        tail.lineTo(m_tailX, height() - 2);
        tail.closeSubpath();
        path = path.united(tail);
        // soft shadow
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 40));
        p.drawPath(path.translated(2, 2));
        QLinearGradient g(0, m_box.top(), 0, m_box.bottom());
        g.setColorAt(0, QColor(254, 254, 255));
        g.setColorAt(1, QColor(228, 229, 240));
        p.setBrush(g);
        p.setPen(QColor(118, 118, 118));
        p.drawPath(path);
        // info icon, title
        p.drawPixmap(m_box.left() + 10, m_box.top() + 11, QIcon::fromTheme(QStringLiteral("dialog-information")).pixmap(16, 16));
        p.setFont(m_titleFont);
        p.setPen(QColor(0x1e, 0x39, 0x9b));
        p.drawText(QRect(m_box.left() + 31, m_box.top() + 8, m_box.width() - 80, 22), Qt::AlignLeft | Qt::AlignVCenter, m_title);
        // wrench + close
        p.setPen(QPen(m_hotClose ? QColor(0x40, 0x40, 0x40) : QColor(0x8c, 0x8c, 0x8c), 1.6, Qt::SolidLine, Qt::RoundCap));
        const QRect c = closeRect().adjusted(3, 3, -3, -3);
        p.drawLine(c.topLeft(), c.bottomRight());
        p.drawLine(c.topRight(), c.bottomLeft());
        p.setPen(QPen(QColor(0x8c, 0x8c, 0x8c), 2.2, Qt::SolidLine, Qt::RoundCap));
        const QPoint wr(m_box.right() - 44, m_box.top() + 11);
        p.drawLine(wr + QPoint(2, 2), wr + QPoint(8, 8));
        p.drawEllipse(QRectF(wr.x() - 1.5, wr.y() - 1.5, 5, 5));
        // body
        p.setFont(m_bodyFont);
        p.setPen(QColor(0x1e, 0x1e, 0x1e));
        p.drawText(QRect(m_box.left() + 31, m_box.top() + 33, m_box.width() - 40, m_box.height() - 36), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, m_text);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const bool hot = closeRect().contains(e->position().toPoint());
        if (hot != m_hotClose) {
            m_hotClose = hot;
            update();
        }
        setCursor(!hot && m_clicked ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (!closeRect().contains(e->position().toPoint()) && m_clicked) {
            m_clicked();
        }
        close();
    }

private:
    QString m_title, m_text;
    std::function<void()> m_clicked;
    QFont m_titleFont, m_bodyFont;
    QRect m_box;
    int m_tailX = 0;
    bool m_hotClose = false;
};

inline void balloon(const QString &title, const QString &text, std::function<void()> clicked = {})
{
    auto *b = new Balloon(title, text, std::move(clicked));
    b->showAt(Balloon::trayTip());
}

class Tray : public QObject
{
public:
    explicit Tray(QObject *parent)
        : QObject(parent)
    {
        auto *n = Solid::DeviceNotifier::instance();
        connect(n, &Solid::DeviceNotifier::deviceAdded, this, &Tray::scheduleUpdate);
        connect(n, &Solid::DeviceNotifier::deviceRemoved, this, &Tray::scheduleUpdate);
        m_delay.setSingleShot(true);
        m_delay.setInterval(700);
        connect(&m_delay, &QTimer::timeout, this, &Tray::update);
        update();
    }

private:
    void scheduleUpdate()
    {
        m_delay.start();
    }
    void update()
    {
        const bool any = !removableDrives().isEmpty();
        if (any && !m_sni) {
            m_sni = new KStatusNotifierItem(QStringLiteral("seven-safely-remove"), this);
            m_sni->setCategory(KStatusNotifierItem::Hardware);
            m_sni->setIconByName(QStringLiteral("seven-safely-remove"));
            m_sni->setTitle(tr7("Safely Remove Hardware and Eject Media"));
            m_sni->setToolTip(QStringLiteral("seven-safely-remove"), tr7("Safely Remove Hardware and Eject Media"), QString());
            // Windows 7 keeps it among the hidden icons (under ▲)
            m_sni->setStatus(KStatusNotifierItem::Passive);
            m_sni->setStandardActionsEnabled(false);
            m_menu = new QMenu; // owned by the item
            m_sni->setContextMenu(m_menu);
            connect(m_menu, &QMenu::aboutToShow, this, &Tray::fillMenu);
            connect(m_sni, &KStatusNotifierItem::activateRequested, this, [this](bool, const QPoint &) {
                fillMenu();
                // Windows 7 (TrackPopupMenu at the cursor, bottom-aligned): the menu's bottom-left corner at the
                // click, moved left as far as needed to stay on the screen
                const QPoint c = QCursor::pos();
                const QSize sz = m_menu->sizeHint();
                QRect avail = QGuiApplication::screenAt(c) ? QGuiApplication::screenAt(c)->geometry() : QRect(0, 0, 1920, 1080);
                int x = qMin(c.x(), avail.right() - sz.width());
                int y = c.y() - sz.height();
                if (y < avail.top()) {
                    y = c.y();
                }
                m_menu->popup(QPoint(qMax(avail.left(), x), y));
            });
        } else if (!any && m_sni) {
            delete m_sni; // takes its menu along
            m_menu = nullptr;
        }
    }
    void fillMenu()
    {
        m_menu->clear();
        QAction *open = m_menu->addAction(QIcon::fromTheme(QStringLiteral("seven-cpi-printers")), tr7("Open Devices and Printers"));
        QFont bold = open->font();
        bold.setBold(true);
        open->setFont(bold);
        connect(open, &QAction::triggered, this, [] {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--devices")});
        });
        for (const RemovableDrive &r : removableDrives()) {
            m_menu->addSeparator();
            const bool optical = r.drive.is<Solid::OpticalDrive>();
            QAction *ej = m_menu->addAction(QIcon::fromTheme(optical ? QStringLiteral("seven-bk-dvd") : QStringLiteral("drive-removable-media-usb")),
                                            tr7("Eject %1").arg(r.name));
            const QString udi = r.drive.udi();
            connect(ej, &QAction::triggered, this, [this, udi] {
                eject(udi);
            });
            for (const Solid::Device &v : r.volumes) {
                QAction *va = m_menu->addAction(QStringLiteral("      - %1").arg(volumeName(v)));
                va->setEnabled(false);
            }
        }
    }
    void eject(const QString &driveUdi)
    {
        RemovableDrive target;
        for (const RemovableDrive &r : removableDrives()) {
            if (r.drive.udi() == driveUdi) {
                target = r;
            }
        }
        if (!target.drive.isValid()) {
            return;
        }
        // unmount every mounted volume first (lazily, one after the other)
        QList<Solid::Device> mounted;
        for (const Solid::Device &v : target.volumes) {
            const auto *acc = v.as<Solid::StorageAccess>();
            if (acc && acc->isAccessible()) {
                mounted << v;
            }
        }
        auto pending = std::make_shared<int>(mounted.size());
        auto failed = std::make_shared<bool>(false);
        const QString name = target.name;
        const QString block = target.blockDevice;
        const bool optical = target.drive.is<Solid::OpticalDrive>();
        auto finish = [this, pending, failed, name, block, optical, driveUdi] {
            if (*failed) {
                balloon(tr7("Problem Ejecting \"%1\"").arg(name),
                        tr7("This device is currently in use. Close any programs or windows that might be using the device, and then try again."));
                return;
            }
            if (optical) {
                Solid::Device d(driveUdi);
                if (auto *od = d.as<Solid::OpticalDrive>()) {
                    od->eject();
                }
            }
            if (!block.isEmpty()) {
                QProcess::startDetached(QStringLiteral("udisksctl"), {QStringLiteral("power-off"), QStringLiteral("-b"), block, QStringLiteral("--no-user-interaction")});
            }
            balloon(tr7("Safe To Remove Hardware"),
                    tr7("The \"%1\" device can now be safely removed from the computer.").arg(optical ? name : tr7("USB Mass Storage Device", "safely remove")));
            scheduleUpdate();
        };
        if (mounted.isEmpty()) {
            finish();
            return;
        }
        for (const Solid::Device &v : mounted) {
            auto *acc = const_cast<Solid::StorageAccess *>(v.as<Solid::StorageAccess>());
            connect(acc, &Solid::StorageAccess::teardownDone, this, [pending, failed, finish](Solid::ErrorType error, const QVariant &, const QString &) {
                if (error != Solid::NoError) {
                    *failed = true;
                }
                if (--*pending == 0) {
                    finish();
                }
            }, Qt::SingleShotConnection);
            acc->teardown();
        }
    }

    QPointer<KStatusNotifierItem> m_sni;
    QMenu *m_menu = nullptr;
    QTimer m_delay;
};

/*
 * "Цей пристрій може працювати швидше" (Windows 7 balloon): a Hi-Speed capable USB device (bcdUSB >= 2.00) with
 * a bulk function (storage, still image, video) running at full speed (12 Mbit/s) only, i.e. plugged into a
 * USB 1.1 port or hub. Keyboards, mice and Bluetooth radios are full-speed by design and are left out, like Windows
 * (which reads the device qualifier descriptor). Clicking the balloon opens Devices and Printers.
 */
class SpeedWatcher : public QObject
{
public:
    explicit SpeedWatcher(QObject *parent)
        : QObject(parent)
    {
        m_known = slowDevices(); // only devices plugged in from now on
        m_timer.setInterval(2000);
        connect(&m_timer, &QTimer::timeout, this, &SpeedWatcher::poll);
        m_timer.start();
    }

    static QString read(const QString &dir, const char *file)
    {
        QFile f(dir + QLatin1Char('/') + QLatin1String(file));
        return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readAll()).trimmed() : QString();
    }
    /** sysfs names of the devices that could be faster */
    static QSet<QString> slowDevices()
    {
        QSet<QString> out;
        const QDir root(QStringLiteral("/sys/bus/usb/devices"));
        for (const QString &n : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (n.contains(QLatin1Char(':')) || n.startsWith(QLatin1String("usb"))) {
                continue;
            }
            const QString d = root.filePath(n);
            if (read(d, "speed") != QLatin1String("12") || read(d, "version").toDouble() < 2.0) {
                continue;
            }
            bool bulk = false;
            for (const QString &i : QDir(d).entryList({n + QStringLiteral(":*")}, QDir::Dirs)) {
                const QString cls = read(d + QLatin1Char('/') + i, "bInterfaceClass");
                bulk = bulk || cls == QLatin1String("08") || cls == QLatin1String("06") || cls == QLatin1String("0e");
            }
            if (bulk) {
                out.insert(n);
            }
        }
        return out;
    }

private:
    void poll()
    {
        const QSet<QString> now = slowDevices();
        for (const QString &n : now) {
            if (!m_known.contains(n)) {
                notify();
            }
        }
        m_known = now;
    }
    void notify()
    {
        balloon(tr7("This device can perform faster"),
                tr7("This USB device can perform faster if you connect it to a Hi-Speed USB 2.0 port.\nFor a list of available ports, click here."),
                [] {
                    QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--devices")});
                });
    }

    QSet<QString> m_known;
    QTimer m_timer;
};
}
