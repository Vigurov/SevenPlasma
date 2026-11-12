/*
 * "Date and Time" of Windows 7: the dialog with its three tabs and the "Date and Time Settings",
 * "Time Zone Settings" and "Internet Time Settings" windows.
 * Time, date, time zone and synchronisation go through timedatectl (polkit asks for the password,
 * like the UAC shield on these buttons). Additional clocks live in the taskbar clock's settings,
 * so its tooltip and popup show them.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7trayicons.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QGroupBox>
#include <QMessageBox>
#include <QPainter>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTabWidget>
#include <QTimeEdit>
#include <QTimeZone>
#include <QTimer>

namespace Win7Time
{
inline const QString kClockWidget = QStringLiteral("io.gitgud.wackyideas.digitalclocklite");

inline QLocale uk()
{
    return QLocale{};
}

inline QString longDate(const QDate &d)
{
    return uk().toString(d, tr7("MMMM d, yyyy"));
}

inline void fixedDialog(QDialog *d, const QString &title, const QString &icon)
{
    d->setWindowTitle(title);
    d->setWindowIcon(QIcon::fromTheme(icon));
    d->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
}

inline QPushButton *shieldButton(const QString &text)
{
    auto *b = new QPushButton(QIcon::fromTheme(QStringLiteral("seven-uac-shield")), text);
    b->setIconSize(QSize(16, 16));
    return b;
}

inline QString run(const QString &cmd, const QStringList &args, int *exitCode = nullptr, QString *err = nullptr)
{
    QProcess p;
    p.start(cmd, args);
    p.waitForFinished(120000); // polkit may be asking for the password
    if (exitCode) {
        *exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    }
    if (err) {
        *err = QString::fromUtf8(p.readAllStandardError()).trimmed();
    }
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

/* ---------------- time zones, named like the Windows 7 list ---------------- */

struct Zone {
    QByteArray iana;
    QString label; // "(UTC+02:00) Гельсінкі, Київ, ..."
};

inline QString offsetText(int secs)
{
    if (secs == 0) {
        return QStringLiteral("(UTC)");
    }
    const int m = qAbs(secs) / 60;
    return QStringLiteral("(UTC%1%2:%3)").arg(secs < 0 ? QStringLiteral("-") : QStringLiteral("+")).arg(m / 60, 2, 10, QLatin1Char('0')).arg(m % 60, 2, 10, QLatin1Char('0'));
}

inline const QList<Zone> &zones()
{
    static QList<Zone> list;
    if (!list.isEmpty()) {
        return list;
    }
    // Windows time zone id -> the cities Windows 7 shows (translated with tr7)
    static const QList<QPair<const char *, const char *>> table{
        {"Dateline Standard Time", "International Date Line West"},
        {"UTC-11", "Coordinated Universal Time-11"},
        {"Hawaiian Standard Time", "Hawaii"},
        {"Alaskan Standard Time", "Alaska"},
        {"Pacific Standard Time (Mexico)", "Baja California"},
        {"Pacific Standard Time", "Pacific Time (US & Canada)"},
        {"US Mountain Standard Time", "Arizona"},
        {"Mountain Standard Time (Mexico)", "Chihuahua, La Paz, Mazatlan"},
        {"Mountain Standard Time", "Mountain Time (US & Canada)"},
        {"Central America Standard Time", "Central America"},
        {"Central Standard Time", "Central Time (US & Canada)"},
        {"Central Standard Time (Mexico)", "Guadalajara, Mexico City, Monterrey"},
        {"Canada Central Standard Time", "Saskatchewan"},
        {"SA Pacific Standard Time", "Bogota, Lima, Quito"},
        {"Eastern Standard Time", "Eastern Time (US & Canada)"},
        {"US Eastern Standard Time", "Indiana (East)"},
        {"Venezuela Standard Time", "Caracas"},
        {"Paraguay Standard Time", "Asuncion"},
        {"Atlantic Standard Time", "Atlantic Time (Canada)"},
        {"Central Brazilian Standard Time", "Cuiaba"},
        {"SA Western Standard Time", "Georgetown, La Paz, Manaus, San Juan"},
        {"Pacific SA Standard Time", "Santiago"},
        {"Newfoundland Standard Time", "Newfoundland"},
        {"E. South America Standard Time", "Brasilia"},
        {"Argentina Standard Time", "Buenos Aires"},
        {"SA Eastern Standard Time", "Cayenne, Fortaleza"},
        {"Greenland Standard Time", "Greenland"},
        {"Montevideo Standard Time", "Montevideo"},
        {"UTC-02", "Coordinated Universal Time-02"},
        {"Azores Standard Time", "Azores"},
        {"Cape Verde Standard Time", "Cape Verde Is."},
        {"Morocco Standard Time", "Casablanca"},
        {"UTC", "Coordinated Universal Time"},
        {"GMT Standard Time", "Dublin, Edinburgh, Lisbon, London"},
        {"Greenwich Standard Time", "Monrovia, Reykjavik"},
        {"W. Europe Standard Time", "Amsterdam, Berlin, Bern, Rome, Stockholm, Vienna"},
        {"Central Europe Standard Time", "Belgrade, Bratislava, Budapest, Ljubljana, Prague"},
        {"Romance Standard Time", "Brussels, Copenhagen, Madrid, Paris"},
        {"Central European Standard Time", "Sarajevo, Skopje, Warsaw, Zagreb"},
        {"W. Central Africa Standard Time", "West Central Africa"},
        {"Namibia Standard Time", "Windhoek"},
        {"Jordan Standard Time", "Amman"},
        {"GTB Standard Time", "Athens, Bucharest"},
        {"Middle East Standard Time", "Beirut"},
        {"Egypt Standard Time", "Cairo"},
        {"Syria Standard Time", "Damascus"},
        {"South Africa Standard Time", "Harare, Pretoria"},
        {"FLE Standard Time", "Helsinki, Kyiv, Riga, Sofia, Tallinn, Vilnius"},
        {"Turkey Standard Time", "Istanbul"},
        {"Israel Standard Time", "Jerusalem"},
        {"E. Europe Standard Time", "Chisinau"},
        {"Kaliningrad Standard Time", "Kaliningrad"},
        {"Belarus Standard Time", "Minsk"},
        {"Arabic Standard Time", "Baghdad"},
        {"Arab Standard Time", "Kuwait, Riyadh"},
        {"Russian Standard Time", "Moscow, St. Petersburg"},
        {"E. Africa Standard Time", "Nairobi"},
        {"Iran Standard Time", "Tehran"},
        {"Arabian Standard Time", "Abu Dhabi, Muscat"},
        {"Azerbaijan Standard Time", "Baku"},
        {"Mauritius Standard Time", "Port Louis"},
        {"Georgian Standard Time", "Tbilisi"},
        {"Caucasus Standard Time", "Yerevan"},
        {"Afghanistan Standard Time", "Kabul"},
        {"Ekaterinburg Standard Time", "Ekaterinburg"},
        {"Pakistan Standard Time", "Islamabad, Karachi"},
        {"West Asia Standard Time", "Tashkent"},
        {"India Standard Time", "Chennai, Kolkata, Mumbai, New Delhi"},
        {"Sri Lanka Standard Time", "Sri Jayawardenepura"},
        {"Nepal Standard Time", "Kathmandu"},
        {"Central Asia Standard Time", "Astana"},
        {"Bangladesh Standard Time", "Dhaka"},
        {"N. Central Asia Standard Time", "Novosibirsk"},
        {"Myanmar Standard Time", "Yangon (Rangoon)"},
        {"SE Asia Standard Time", "Bangkok, Hanoi, Jakarta"},
        {"North Asia Standard Time", "Krasnoyarsk"},
        {"China Standard Time", "Beijing, Chongqing, Hong Kong, Urumqi"},
        {"North Asia East Standard Time", "Irkutsk"},
        {"Singapore Standard Time", "Kuala Lumpur, Singapore"},
        {"W. Australia Standard Time", "Perth"},
        {"Taipei Standard Time", "Taipei"},
        {"Ulaanbaatar Standard Time", "Ulaanbaatar"},
        {"Tokyo Standard Time", "Osaka, Sapporo, Tokyo"},
        {"Korea Standard Time", "Seoul"},
        {"Yakutsk Standard Time", "Yakutsk"},
        {"Cen. Australia Standard Time", "Adelaide"},
        {"AUS Central Standard Time", "Darwin"},
        {"E. Australia Standard Time", "Brisbane"},
        {"AUS Eastern Standard Time", "Canberra, Melbourne, Sydney"},
        {"West Pacific Standard Time", "Guam, Port Moresby"},
        {"Tasmania Standard Time", "Hobart"},
        {"Vladivostok Standard Time", "Vladivostok"},
        {"Magadan Standard Time", "Magadan"},
        {"Central Pacific Standard Time", "Solomon Is., New Caledonia"},
        {"New Zealand Standard Time", "Auckland, Wellington"},
        {"UTC+12", "Coordinated Universal Time+12"},
        {"Fiji Standard Time", "Fiji"},
        {"Tonga Standard Time", "Nuku'alofa"},
        {"Samoa Standard Time", "Samoa"},
    };
    const QByteArray current = QTimeZone::systemTimeZoneId();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QList<QPair<int, Zone>> sorted;
    for (const auto &e : table) {
        const QByteArray win(e.first);
        QByteArray iana = QTimeZone::windowsIdToDefaultIanaId(win, QLocale::Ukraine);
        if (iana.isEmpty()) {
            iana = QTimeZone::windowsIdToDefaultIanaId(win);
        }
        if (iana.isEmpty() || !QTimeZone(iana).isValid()) {
            continue;
        }
        if (QTimeZone::ianaIdToWindowsId(current) == win) {
            iana = current; // keep the exact zone the system uses
        }
        const int off = QTimeZone(iana).standardTimeOffset(now);
        sorted.append({off, {iana, offsetText(off) + QLatin1Char(' ') + tr7(e.second)}});
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });
    bool haveCurrent = false;
    for (const auto &s : std::as_const(sorted)) {
        list.append(s.second);
        haveCurrent = haveCurrent || s.second.iana == current;
    }
    if (!haveCurrent && QTimeZone(current).isValid()) { // a zone Windows does not know: show it by its city
        const int off = QTimeZone(current).standardTimeOffset(now);
        list.append({current, offsetText(off) + QLatin1Char(' ') + QString::fromUtf8(current).section(QLatin1Char('/'), -1).replace(QLatin1Char('_'), QLatin1Char(' '))});
    }
    return list;
}

inline QString zoneLabel(const QByteArray &iana)
{
    for (const Zone &z : zones()) {
        if (z.iana == iana) {
            return z.label;
        }
    }
    return QString::fromUtf8(iana);
}

inline void fillZones(QComboBox *box, const QByteArray &select)
{
    for (const Zone &z : zones()) {
        box->addItem(z.label, z.iana);
    }
    int i = box->findData(select);
    if (i < 0) { // e.g. Europe/Kiev vs Europe/Kyiv: same Windows zone
        const QByteArray win = QTimeZone::ianaIdToWindowsId(select);
        for (int k = 0; k < box->count() && i < 0; ++k) {
            if (QTimeZone::ianaIdToWindowsId(box->itemData(k).toByteArray()) == win) {
                i = k;
            }
        }
    }
    box->setCurrentIndex(qMax(0, i));
    box->setMaxVisibleItems(20);
}

/** "Daylight Saving Time ends on 25 October 2026 at 4:00. The clock is set to go back 1 hour at that time." */
inline QString dstText(const QTimeZone &tz)
{
    if (!tz.hasTransitions()) {
        return tr7("Daylight Saving Time is not observed in this time zone.");
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QTimeZone::OffsetData next = tz.nextTransition(now);
    if (!next.atUtc.isValid() || next.atUtc > now.addYears(1)) {
        return tr7("Daylight Saving Time is not observed in this time zone.");
    }
    const int before = tz.offsetFromUtc(next.atUtc.addSecs(-1));
    const int after = next.offsetFromUtc;
    const QDateTime local = next.atUtc.addSecs(before).toUTC();
    const int hours = qAbs(after - before) / 3600;
    const bool winter = after < before;
    return tr7("The change to %1 time happens on %2 at %3. The clock is set to go %6 %4 %5 at that time.")
        .arg(winter ? tr7("standard") : tr7("daylight saving"), longDate(local.date()), local.time().toString(QStringLiteral("H:mm")))
        .arg(hours)
        .arg(hours == 1 ? tr7("hour") : tr7("hours"), winter ? tr7("back") : tr7("forward"));
}

/* ---------------- the analog clock of the taskbar popup ---------------- */

class AnalogClock : public QWidget
{
public:
    QByteArray zone; // empty: local time
    QTime fixed;     // shown instead of the live time when valid (Date and Time Settings)

    explicit AnalogClock(int size = 128)
    {
        setFixedSize(size, size);
        auto *t = new QTimer(this);
        connect(t, &QTimer::timeout, this, qOverload<>(&QWidget::update));
        t->start(1000);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        // the clock of the taskbar (AeroThemePlasma installs it to /usr, a development copy may be in ~/.local)
        static QSvgRenderer svg(QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("plasma/plasmoids/io.gitgud.wackyideas.digitalclocklite/contents/ui/svgs/clock.svg")));
        QPainter p(this);
        p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        const QRectF r(0, 0, width(), height());
        svg.render(&p, QStringLiteral("clockface"), r);
        const QTime t = fixed.isValid() ? fixed : (zone.isEmpty() ? QTime::currentTime() : QDateTime::currentDateTime().toTimeZone(QTimeZone(zone)).time());
        const qreal k = width() / 128.0;
        auto hand = [&](qreal angle, qreal len, qreal w, const QColor &c) {
            p.save();
            p.translate(r.center());
            p.rotate(angle);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawRoundedRect(QRectF(-w / 2, -len * k, w, len * k), 1, 1);
            p.restore();
        };
        hand((t.hour() % 12 + t.minute() / 60.0) * 30, 36, 2.2 * k, QColor(0x5c, 0x6c, 0x74, 0xe8));
        hand(t.minute() * 6, 47, 2.2 * k, QColor(0x5c, 0x6c, 0x74, 0xe8));
        p.save(); // the thin second hand also sticks out behind the centre
        p.translate(r.center());
        p.rotate(t.second() * 6);
        p.setPen(QPen(QColor(0x54, 0x67, 0x70, 0xbf), qMax(1.0, k)));
        p.drawLine(QPointF(0, 15 * k), QPointF(0, -50 * k));
        p.restore();
        const qreal dot = 5 * k;
        svg.render(&p, QStringLiteral("clockdot"), QRectF(r.center().x() - dot / 2, r.center().y() - dot / 2, dot, dot));
        svg.render(&p, QStringLiteral("clockshine"), r);
    }
};

/* ---------------- Date and Time Settings ---------------- */

/** The month view of the Windows 7 "Date and Time Settings" window */
class MonthView : public QWidget
{
public:
    QDate selected;

    explicit MonthView(QDate d)
        : selected(d)
        , m_month(d.year(), d.month(), 1)
    {
        setMouseTracking(true);
        QFont f = font();
        f.setPixelSize(12);
        setFont(f);
        setFixedSize(7 * kCell + 12, 40 + 6 * kRow + 8);
    }

protected:
    static constexpr int kCell = 24;
    static constexpr int kRow = 17;

    QDate firstShown() const
    {
        const int back = m_month.dayOfWeek() - 1;
        return m_month.addDays(back == 0 ? -7 : -back);
    }
    QRect cell(int i) const
    {
        return QRect(6 + (i % 7) * kCell, 40 + (i / 7) * kRow, kCell, kRow);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        p.setPen(QColor(0xb4, 0xb4, 0xb4));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        QString title = uk().standaloneMonthName(m_month.month()) + QLatin1Char(' ') + QString::number(m_month.year());
        title[0] = title.at(0).toUpper();
        p.setPen(Qt::black);
        p.drawText(QRect(0, 4, width(), 18), Qt::AlignCenter, title);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Qt::black);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({QPointF(12, 13), QPointF(16, 9.5), QPointF(16, 16.5)}));
        p.drawPolygon(QPolygonF({QPointF(width() - 12, 13), QPointF(width() - 16, 9.5), QPointF(width() - 16, 16.5)}));
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setBrush(Qt::NoBrush);
        static const QStringList days{tr7("Mon"), tr7("Tue"), tr7("Wed"), tr7("Thu"), tr7("Fri"), tr7("Sat"), tr7("Sun", "weekday")};
        p.setPen(Qt::black);
        for (int i = 0; i < 7; ++i) {
            p.drawText(QRect(6 + i * kCell, 22, kCell, 16), Qt::AlignCenter, days.at(i));
        }
        const QDate start = firstShown();
        for (int i = 0; i < 42; ++i) {
            const QDate d = start.addDays(i);
            const QRect r = cell(i).adjusted(1, 0, -1, 0);
            if (d == m_hover && d != selected) {
                p.fillRect(r, QColor(0xe5, 0xf3, 0xfb));
                p.setPen(QColor(0x9d, 0xc6, 0xe8));
                p.drawRect(r.adjusted(0, 0, -1, -1));
            }
            if (d == selected) {
                p.fillRect(r, QColor(0xdc, 0xeb, 0xfc));
                p.setPen(QColor(0x52, 0x6c, 0x8a));
                p.drawRect(r.adjusted(0, 0, -1, -1));
            }
            p.setPen(d.month() == m_month.month() ? QColor(Qt::black) : QColor(0x8d, 0x8d, 0x8d));
            p.drawText(cell(i), Qt::AlignCenter, QString::number(d.day()));
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        QDate h;
        for (int i = 0; i < 42; ++i) {
            if (cell(i).contains(e->position().toPoint())) {
                h = firstShown().addDays(i);
            }
        }
        if (h != m_hover) {
            m_hover = h;
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = QDate();
        update();
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        const QPoint pos = e->position().toPoint();
        if (pos.y() < 22) {
            if (pos.x() < 30) {
                m_month = m_month.addMonths(-1);
            } else if (pos.x() > width() - 30) {
                m_month = m_month.addMonths(1);
            }
            update();
            return;
        }
        for (int i = 0; i < 42; ++i) {
            if (cell(i).contains(pos)) {
                selected = firstShown().addDays(i);
                m_month = QDate(selected.year(), selected.month(), 1);
                update();
            }
        }
    }

    void wheelEvent(QWheelEvent *e) override
    {
        m_month = m_month.addMonths(e->angleDelta().y() > 0 ? -1 : 1);
        update();
    }

private:
    QDate m_month;
    QDate m_hover;
};

inline bool ntpActive()
{
    return run(QStringLiteral("timedatectl"), {QStringLiteral("show"), QStringLiteral("-p"), QStringLiteral("NTP"), QStringLiteral("--value")}) == QLatin1String("yes");
}

inline bool setTime(QWidget *parent, const QDateTime &when)
{
    if (ntpActive()) {
        if (QMessageBox::question(parent, tr7("Date and Time"),
                                  tr7("Synchronization with an Internet time server is on. To set the time manually, it must be turned off.\n\nTurn synchronization off?"))
            != QMessageBox::Yes) {
            return false;
        }
        int code = 0;
        run(QStringLiteral("timedatectl"), {QStringLiteral("set-ntp"), QStringLiteral("false")}, &code);
        if (code != 0) {
            return false;
        }
    }
    int code = 0;
    QString err;
    run(QStringLiteral("timedatectl"), {QStringLiteral("set-time"), when.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))}, &code, &err);
    if (code != 0 && !err.isEmpty() && !err.contains(QLatin1String("authoriz"), Qt::CaseInsensitive)) {
        QMessageBox::warning(parent, tr7("Date and Time"), tr7("The time could not be changed:\n%1").arg(err));
    }
    return code == 0;
}

class DateTimeSettings : public QDialog
{
public:
    std::function<void()> openCalendarSettings;

    explicit DateTimeSettings(QWidget *parent)
        : QDialog(parent)
    {
        fixedDialog(this, tr7("Date and Time Settings"), QStringLiteral("seven-datetime"));
        setMinimumWidth(450);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(12, 12, 12, 12);
        v->addWidget(new QLabel(tr7("Set the date and time:")));
        v->addSpacing(8);
        auto *row = new QHBoxLayout;
        auto *left = new QVBoxLayout;
        left->addWidget(new QLabel(tr7("Date:")));
        m_month = new MonthView(QDate::currentDate());
        left->addWidget(m_month);
        left->addStretch(1);
        row->addLayout(left);
        row->addSpacing(18);
        auto *right = new QVBoxLayout;
        right->addWidget(new QLabel(tr7("Time:")));
        m_clock = new AnalogClock(128);
        right->addWidget(m_clock, 0, Qt::AlignHCenter);
        m_time = new QTimeEdit(QTime::currentTime());
        m_time->setDisplayFormat(QStringLiteral("HH:mm:ss"));
        m_time->setFixedWidth(80);
        right->addWidget(m_time, 0, Qt::AlignHCenter);
        right->addStretch(1);
        row->addLayout(right);
        v->addLayout(row);
        v->addSpacing(14);
        auto *link = new Link(tr7("Change calendar settings"), [this] {
            if (openCalendarSettings) {
                openCalendarSettings();
            }
        });
        link->setWordWrap(false);
        v->addWidget(link, 0, Qt::AlignLeft);
        v->addSpacing(8);
        auto *btns = new QHBoxLayout;
        btns->addStretch(1);
        m_ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        m_ok->setMinimumWidth(75);
        cancel->setMinimumWidth(75);
        m_ok->setDefault(true);
        btns->addWidget(m_ok);
        btns->addWidget(cancel);
        v->addLayout(btns);
        setFixedSize(qMax(450, sizeHint().width()), sizeHint().height());

        // the clock runs until the time is edited, then shows the edited time
        auto *tick = new QTimer(this);
        connect(tick, &QTimer::timeout, this, [this] {
            if (!m_edited) {
                m_time->blockSignals(true);
                m_time->setTime(QTime::currentTime());
                m_time->blockSignals(false);
            } else {
                m_clock->fixed = m_time->time();
            }
        });
        tick->start(1000);
        connect(m_time, &QTimeEdit::timeChanged, this, [this](const QTime &t) {
            m_edited = true;
            m_clock->fixed = t;
            m_clock->update();
        });
        connect(m_ok, &QPushButton::clicked, this, [this] {
            if (setTime(this, QDateTime(m_month->selected, m_edited ? m_time->time() : QTime::currentTime()))) {
                accept();
            }
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

private:
    MonthView *m_month;
    AnalogClock *m_clock;
    QTimeEdit *m_time;
    QPushButton *m_ok;
    bool m_edited = false;
};

/* ---------------- Time Zone Settings ---------------- */

class TimeZoneSettings : public QDialog
{
public:
    explicit TimeZoneSettings(QWidget *parent)
        : QDialog(parent)
    {
        fixedDialog(this, tr7("Time Zone Settings"), QStringLiteral("seven-datetime"));
        setFixedWidth(460);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(12, 12, 12, 12);
        v->addWidget(new QLabel(tr7("Set the time zone:")));
        v->addSpacing(12);
        v->addWidget(new QLabel(tr7("Time zone:")));
        m_zone = new QComboBox;
        fillZones(m_zone, QTimeZone::systemTimeZoneId());
        v->addWidget(m_zone);
        v->addSpacing(12);
        auto *now = new QHBoxLayout;
        now->addWidget(new QLabel(tr7("Current date and time:")));
        now->addSpacing(20);
        m_now = new QLabel;
        now->addWidget(m_now, 1);
        v->addLayout(now);
        v->addSpacing(30);
        auto *btns = new QHBoxLayout;
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        ok->setMinimumWidth(75);
        cancel->setMinimumWidth(75);
        ok->setDefault(true);
        btns->addWidget(ok);
        btns->addWidget(cancel);
        v->addLayout(btns);
        updateNow();
        auto *t = new QTimer(this);
        connect(t, &QTimer::timeout, this, [this] {
            updateNow();
        });
        t->start(1000);
        setFixedHeight(sizeHint().height());
        connect(m_zone, &QComboBox::currentIndexChanged, this, [this] {
            updateNow();
        });
        connect(ok, &QPushButton::clicked, this, [this] {
            const QByteArray zone = m_zone->currentData().toByteArray();
            if (zone != QTimeZone::systemTimeZoneId()) {
                int code = 0;
                run(QStringLiteral("timedatectl"), {QStringLiteral("set-timezone"), QString::fromLatin1(zone)}, &code);
                if (code != 0) {
                    return;
                }
                qputenv("TZ", zone); // this process shows the new zone at once
            }
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

private:
    void updateNow()
    {
        const QDateTime t = QDateTime::currentDateTime().toTimeZone(QTimeZone(m_zone->currentData().toByteArray()));
        m_now->setText(longDate(t.date()) + QStringLiteral(", ") + t.time().toString(QStringLiteral("H:mm")));
    }

    QComboBox *m_zone;
    QLabel *m_now;
};

/* ---------------- Internet Time Settings ---------------- */

inline QString timeServer()
{
    QString s = run(QStringLiteral("timedatectl"), {QStringLiteral("show-timesync"), QStringLiteral("-p"), QStringLiteral("ServerName"), QStringLiteral("--value")});
    if (s.isEmpty()) {
        const QString fallback = run(QStringLiteral("timedatectl"), {QStringLiteral("show-timesync"), QStringLiteral("-p"), QStringLiteral("FallbackNTPServers"), QStringLiteral("--value")});
        s = fallback.section(QLatin1Char(' '), 0, 0);
    }
    return s.isEmpty() ? QStringLiteral("ntp.ubuntu.com") : s;
}

class InternetTimeSettings : public QDialog
{
public:
    explicit InternetTimeSettings(QWidget *parent)
        : QDialog(parent)
    {
        fixedDialog(this, tr7("Internet Time Settings"), QStringLiteral("seven-datetime"));
        setFixedWidth(440);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(12, 12, 12, 12);
        v->addWidget(new QLabel(tr7("Configure Internet time settings:")));
        v->addSpacing(6);
        m_sync = new QCheckBox(tr7("Synchronize with an Internet time server"));
        m_sync->setChecked(ntpActive());
        v->addWidget(m_sync);
        auto *row = new QHBoxLayout;
        row->setContentsMargins(18, 0, 0, 0);
        row->addWidget(new QLabel(tr7("Server:")));
        m_server = new QComboBox;
        m_server->setEditable(true);
        m_oldServer = timeServer();
        m_server->addItems({m_oldServer});
        for (const QString &s : {QStringLiteral("ntp.ubuntu.com"), QStringLiteral("pool.ntp.org"), QStringLiteral("time.google.com"), QStringLiteral("time.cloudflare.com"), QStringLiteral("time.windows.com")}) {
            if (m_server->findText(s) < 0) {
                m_server->addItem(s);
            }
        }
        m_server->setMinimumWidth(190);
        row->addWidget(m_server, 1);
        auto *now = new QPushButton(tr7("Update now"));
        row->addWidget(now);
        v->addLayout(row);
        v->addSpacing(10);
        m_status = new QLabel(tr7("This computer is set to automatically synchronize on a scheduled basis."));
        m_status->setWordWrap(true);
        v->addWidget(m_status);
        v->addSpacing(28);
        auto *btns = new QHBoxLayout;
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        ok->setMinimumWidth(75);
        cancel->setMinimumWidth(75);
        ok->setDefault(true);
        btns->addWidget(ok);
        btns->addWidget(cancel);
        v->addLayout(btns);
        setFixedHeight(sizeHint().height());
        auto enable = [this, now] {
            m_server->setEnabled(m_sync->isChecked());
            now->setEnabled(m_sync->isChecked());
        };
        enable();
        connect(m_sync, &QCheckBox::toggled, this, enable);
        connect(now, &QPushButton::clicked, this, [this] {
            applyServer();
            int code = 0;
            run(QStringLiteral("pkexec"), {QStringLiteral("systemctl"), QStringLiteral("restart"), QStringLiteral("systemd-timesyncd")}, &code);
            if (code != 0) {
                return;
            }
            m_status->setText(tr7("Synchronizing with %1...").arg(m_server->currentText()));
            QTimer::singleShot(3000, this, [this] {
                const bool ok = run(QStringLiteral("timedatectl"), {QStringLiteral("show"), QStringLiteral("-p"), QStringLiteral("NTPSynchronized"), QStringLiteral("--value")}) == QLatin1String("yes");
                const QDateTime t = QDateTime::currentDateTime();
                m_status->setText(ok ? tr7("The clock was successfully synchronized with %1 on %2 at %3.").arg(m_server->currentText(), longDate(t.date()), t.time().toString(QStringLiteral("H:mm")))
                                     : tr7("An error occurred while Windows was synchronizing with %1.").arg(m_server->currentText()));
            });
        });
        connect(ok, &QPushButton::clicked, this, [this] {
            applyServer();
            if (m_sync->isChecked() != ntpActive()) {
                run(QStringLiteral("timedatectl"), {QStringLiteral("set-ntp"), m_sync->isChecked() ? QStringLiteral("true") : QStringLiteral("false")});
            }
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

private:
    void applyServer()
    {
        const QString server = m_server->currentText().trimmed();
        if (server.isEmpty() || server == m_oldServer || server.contains(QRegularExpression(QStringLiteral("[^A-Za-z0-9.:-]")))) {
            return;
        }
        // a drop-in for systemd-timesyncd
        int code = 0;
        run(QStringLiteral("pkexec"),
            {QStringLiteral("sh"), QStringLiteral("-c"),
             QStringLiteral("mkdir -p /etc/systemd/timesyncd.conf.d && printf '[Time]\\nNTP=%1\\n' > /etc/systemd/timesyncd.conf.d/win7.conf && systemctl restart systemd-timesyncd").arg(server)},
            &code);
        if (code == 0) {
            m_oldServer = server;
        }
    }

    QCheckBox *m_sync;
    QComboBox *m_server;
    QLabel *m_status;
    QString m_oldServer;
};

/* ---------------- Additional clocks (kept in the taskbar clock's settings) ---------------- */

struct ExtraClock {
    bool show = false;
    QByteArray zone;
    QString name;
};

inline QString findClock()
{
    return QStringLiteral("var c = null; var ps = panels(); for (var i = 0; i < ps.length && !c; ++i) { var ws = ps[i].widgets('%1'); if (ws.length) c = ws[0]; }"
                          " if (c) c.currentConfigGroup = ['Appearance'];")
        .arg(kClockWidget);
}

inline QList<ExtraClock> readExtraClocks()
{
    QList<ExtraClock> list{{false, QTimeZone::systemTimeZoneId(), tr7("Clock 1")}, {false, QTimeZone::systemTimeZoneId(), tr7("Clock 2")}};
    const QString out = Win7Tray::runScript(findClock() + QStringLiteral(" if (c) print(String(c.readConfig('win7ExtraClocks') || ''));"));
    const QJsonArray a = QJsonDocument::fromJson(out.toUtf8()).array();
    for (int i = 0; i < qMin(2, int(a.size())); ++i) {
        const QJsonObject o = a.at(i).toObject();
        list[i] = {o.value(QLatin1String("show")).toBool(), o.value(QLatin1String("zone")).toString().toLatin1(), o.value(QLatin1String("name")).toString()};
    }
    return list;
}

inline void writeExtraClocks(const QList<ExtraClock> &list)
{
    QJsonArray a;
    for (const ExtraClock &c : list) {
        a.append(QJsonObject{{QStringLiteral("show"), c.show}, {QStringLiteral("zone"), QString::fromLatin1(c.zone)}, {QStringLiteral("name"), c.name}});
    }
    const QString json = QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
    Win7Tray::runScript(findClock()
                        + QStringLiteral(" if (c) { c.writeConfig('win7ExtraClocks', %1); c.reloadConfig(); }")
                              .arg(QString::fromUtf8(QJsonDocument(QJsonArray{json}).toJson(QJsonDocument::Compact)).mid(1).chopped(1)));
}

/* ---------------- Date and Time ---------------- */

class DateTimeDialog : public QDialog
{
public:
    std::function<void()> openRegion; // "Change calendar settings"

    explicit DateTimeDialog(int tab, QWidget *parent = nullptr)
        : QDialog(parent)
    {
        fixedDialog(this, tr7("Date and Time"), QStringLiteral("seven-datetime"));
        setAttribute(Qt::WA_DeleteOnClose);
        setFixedWidth(480);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 9, 7, 9);
        auto *tabs = new QTabWidget;
        tabs->addTab(dateTab(), tr7("Date and Time"));
        tabs->addTab(clocksTab(), tr7("Additional Clocks"));
        tabs->addTab(internetTab(), tr7("Internet Time"));
        tabs->setCurrentIndex(tab);
        v->addWidget(tabs, 1);
        auto *btns = new QHBoxLayout;
        btns->setContentsMargins(0, 4, 0, 0);
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        m_apply = new QPushButton(tr7("Apply"));
        for (QPushButton *b : {ok, cancel, m_apply}) {
            b->setMinimumWidth(75);
            btns->addWidget(b);
        }
        ok->setDefault(true);
        m_apply->setEnabled(false);
        v->addLayout(btns);
        setFixedHeight(sizeHint().height());
        connect(ok, &QPushButton::clicked, this, [this] {
            apply();
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_apply, &QPushButton::clicked, this, [this] {
            apply();
        });
    }

private:
    static QLabel *text(const QString &t)
    {
        auto *l = new QLabel(t);
        l->setWordWrap(true);
        return l;
    }

    void refreshDate()
    {
        const QDateTime now = QDateTime::currentDateTime();
        m_date->setText(longDate(now.date()));
        m_time->setText(now.time().toString(QStringLiteral("H:mm:ss")));
        const QByteArray zone = QTimeZone::systemTimeZoneId();
        if (zone != m_shownZone) {
            m_shownZone = zone;
            m_zone->setText(zoneLabel(zone));
            m_dst->setText(dstText(QTimeZone(zone)));
        }
    }

    QWidget *dateTab()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(14, 14, 14, 10);
        auto *top = new QHBoxLayout;
        auto *clock = new AnalogClock(128);
        top->addWidget(clock, 0, Qt::AlignTop);
        top->addSpacing(24);
        auto *info = new QVBoxLayout;
        info->addSpacing(14);
        info->addWidget(new QLabel(tr7("Date:")));
        m_date = new QLabel;
        info->addWidget(m_date);
        info->addSpacing(16);
        info->addWidget(new QLabel(tr7("Time:")));
        m_time = new QLabel;
        info->addWidget(m_time);
        info->addSpacing(10);
        auto *change = shieldButton(tr7("Change date and time..."));
        info->addWidget(change, 0, Qt::AlignRight);
        top->addLayout(info, 1);
        v->addLayout(top);
        v->addSpacing(8);
        auto *zoneHead = new QHBoxLayout;
        zoneHead->addWidget(new QLabel(tr7("Time zone", "group")));
        auto *line = new QFrame;
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet(QStringLiteral("color: #d5dfe5;"));
        zoneHead->addWidget(line, 1);
        v->addLayout(zoneHead);
        v->addSpacing(6);
        m_zone = new QLabel;
        v->addWidget(m_zone);
        v->addSpacing(6);
        auto *changeZone = new QPushButton(tr7("Change time zone..."));
        changeZone->setMinimumWidth(changeZone->sizeHint().width() + 30);
        v->addWidget(changeZone, 0, Qt::AlignRight);
        v->addSpacing(12);
        m_dst = text(QString());
        v->addWidget(m_dst);
        v->addStretch(1);
        refreshDate();
        auto *t = new QTimer(page);
        connect(t, &QTimer::timeout, this, [this] {
            refreshDate();
        });
        t->start(1000);
        connect(change, &QPushButton::clicked, this, [this] {
            DateTimeSettings dlg(this);
            dlg.openCalendarSettings = openRegion;
            dlg.exec();
            refreshDate();
        });
        connect(changeZone, &QPushButton::clicked, this, [this] {
            TimeZoneSettings dlg(this);
            if (dlg.exec() == QDialog::Accepted) {
                m_shownZone.clear();
                refreshDate();
            }
        });
        return page;
    }

    QWidget *clocksTab()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(14, 14, 14, 10);
        v->setSpacing(10);
        v->addWidget(text(tr7("Additional clocks can display the time in other time zones. You can view them by clicking on or hovering over the taskbar clock.")));
        m_oldClocks = readExtraClocks();
        for (int i = 0; i < 2; ++i) {
            auto *box = new QGroupBox;
            auto *bv = new QVBoxLayout(box);
            bv->setSpacing(4);
            auto *show = new QCheckBox(tr7("Show this clock"));
            show->setChecked(m_oldClocks[i].show);
            auto *zoneLabel = new QLabel(tr7("Select time zone:"));
            auto *zone = new QComboBox;
            fillZones(zone, m_oldClocks[i].zone);
            auto *nameLabel = new QLabel(tr7("Enter display name:"));
            auto *name = new QLineEdit(m_oldClocks[i].name.isEmpty() ? tr7("Clock %1").arg(i + 1) : m_oldClocks[i].name);
            name->setMaxLength(15);
            name->setFixedWidth(220);
            bv->addWidget(show);
            bv->addWidget(zoneLabel);
            bv->addWidget(zone);
            bv->addSpacing(4);
            bv->addWidget(nameLabel);
            bv->addWidget(name);
            v->addWidget(box);
            auto enable = [show, zoneLabel, zone, nameLabel, name] {
                for (QWidget *w : std::initializer_list<QWidget *>{zoneLabel, zone, nameLabel, name}) {
                    w->setEnabled(show->isChecked());
                }
            };
            enable();
            connect(show, &QCheckBox::toggled, this, [this, enable] {
                enable();
                m_apply->setEnabled(true);
            });
            connect(zone, &QComboBox::currentIndexChanged, this, [this] {
                m_apply->setEnabled(true);
            });
            connect(name, &QLineEdit::textEdited, this, [this] {
                m_apply->setEnabled(true);
            });
            m_clockRows.append({show, zone, name});
        }
        v->addStretch(1);
        return page;
    }

    QWidget *internetTab()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(14, 14, 14, 10);
        m_internet = text(QString());
        v->addWidget(m_internet);
        v->addSpacing(40);
        m_internet2 = text(QString());
        v->addWidget(m_internet2);
        v->addSpacing(30);
        auto *change = shieldButton(tr7("Change settings..."));
        change->setMinimumWidth(change->sizeHint().width() + 40);
        v->addWidget(change, 0, Qt::AlignRight);
        v->addStretch(1);
        refreshInternet();
        connect(change, &QPushButton::clicked, this, [this] {
            InternetTimeSettings dlg(this);
            dlg.exec();
            refreshInternet();
        });
        return page;
    }

    void refreshInternet()
    {
        if (ntpActive()) {
            m_internet->setText(tr7("This computer is set to automatically synchronize with '%1'.").arg(timeServer()));
            m_internet2->setText(tr7("This computer is set to automatically synchronize on a scheduled basis."));
        } else {
            m_internet->setText(tr7("This computer is not set to synchronize automatically."));
            m_internet2->clear();
        }
    }

    void apply()
    {
        if (!m_apply->isEnabled()) {
            return;
        }
        QList<ExtraClock> clocks;
        for (const ClockRow &r : std::as_const(m_clockRows)) {
            clocks.append({r.show->isChecked(), r.zone->currentData().toByteArray(), r.name->text().trimmed()});
        }
        writeExtraClocks(clocks);
        m_apply->setEnabled(false);
    }

    struct ClockRow {
        QCheckBox *show;
        QComboBox *zone;
        QLineEdit *name;
    };

    QPushButton *m_apply;
    QLabel *m_date;
    QLabel *m_time;
    QLabel *m_zone;
    QLabel *m_dst;
    QByteArray m_shownZone;
    QLabel *m_internet;
    QLabel *m_internet2;
    QList<ExtraClock> m_oldClocks;
    QList<ClockRow> m_clockRows;
};
}
