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

#include "win7trayicons.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QGroupBox>
#include <QMessageBox>
#include <QPainter>
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
    return QLocale(QLocale::Ukrainian, QLocale::Ukraine);
}

inline QString longDate(const QDate &d)
{
    return uk().toString(d, QStringLiteral("d MMMM yyyy 'р.'"));
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
    // Windows time zone id -> the cities Windows 7 shows (Ukrainian)
    static const QList<QPair<const char *, const char *>> table{
        {"Dateline Standard Time", "Лінія зміни дат"},
        {"UTC-11", "Всесвітній координований час -11"},
        {"Hawaiian Standard Time", "Гаваї"},
        {"Alaskan Standard Time", "Аляска"},
        {"Pacific Standard Time (Mexico)", "Нижня Каліфорнія"},
        {"Pacific Standard Time", "Тихоокеанський час (США і Канада)"},
        {"US Mountain Standard Time", "Аризона"},
        {"Mountain Standard Time (Mexico)", "Чіуауа, Ла-Пас, Масатлан"},
        {"Mountain Standard Time", "Гірський час (США і Канада)"},
        {"Central America Standard Time", "Центральна Америка"},
        {"Central Standard Time", "Центральний час (США і Канада)"},
        {"Central Standard Time (Mexico)", "Гвадалахара, Мехіко, Монтеррей"},
        {"Canada Central Standard Time", "Саскачеван"},
        {"SA Pacific Standard Time", "Богота, Ліма, Кіто"},
        {"Eastern Standard Time", "Східний час (США і Канада)"},
        {"US Eastern Standard Time", "Індіана (схід)"},
        {"Venezuela Standard Time", "Каракас"},
        {"Paraguay Standard Time", "Асунсьйон"},
        {"Atlantic Standard Time", "Атлантичний час (Канада)"},
        {"Central Brazilian Standard Time", "Куяба"},
        {"SA Western Standard Time", "Джорджтаун, Ла-Пас, Манаус, Сан-Хуан"},
        {"Pacific SA Standard Time", "Сантьяго"},
        {"Newfoundland Standard Time", "Ньюфаундленд"},
        {"E. South America Standard Time", "Бразиліа"},
        {"Argentina Standard Time", "Буенос-Айрес"},
        {"SA Eastern Standard Time", "Кайєнна, Форталеза"},
        {"Greenland Standard Time", "Гренландія"},
        {"Montevideo Standard Time", "Монтевідео"},
        {"UTC-02", "Всесвітній координований час -02"},
        {"Azores Standard Time", "Азорські острови"},
        {"Cape Verde Standard Time", "Острови Кабо-Верде"},
        {"Morocco Standard Time", "Касабланка"},
        {"UTC", "Всесвітній координований час"},
        {"GMT Standard Time", "Дублін, Единбург, Лісабон, Лондон"},
        {"Greenwich Standard Time", "Монровія, Рейк'явік"},
        {"W. Europe Standard Time", "Амстердам, Берлін, Берн, Рим, Стокгольм, Відень"},
        {"Central Europe Standard Time", "Белград, Братислава, Будапешт, Любляна, Прага"},
        {"Romance Standard Time", "Брюссель, Копенгаген, Мадрид, Париж"},
        {"Central European Standard Time", "Сараєво, Скоп'є, Варшава, Загреб"},
        {"W. Central Africa Standard Time", "Західна Центральна Африка"},
        {"Namibia Standard Time", "Віндгук"},
        {"Jordan Standard Time", "Амман"},
        {"GTB Standard Time", "Афіни, Бухарест"},
        {"Middle East Standard Time", "Бейрут"},
        {"Egypt Standard Time", "Каїр"},
        {"Syria Standard Time", "Дамаск"},
        {"South Africa Standard Time", "Хараре, Преторія"},
        {"FLE Standard Time", "Гельсінкі, Київ, Рига, Софія, Таллінн, Вільнюс"},
        {"Turkey Standard Time", "Стамбул"},
        {"Israel Standard Time", "Єрусалим"},
        {"E. Europe Standard Time", "Кишинів"},
        {"Kaliningrad Standard Time", "Калінінград"},
        {"Belarus Standard Time", "Мінськ"},
        {"Arabic Standard Time", "Багдад"},
        {"Arab Standard Time", "Кувейт, Ер-Ріяд"},
        {"Russian Standard Time", "Москва, Санкт-Петербург"},
        {"E. Africa Standard Time", "Найробі"},
        {"Iran Standard Time", "Тегеран"},
        {"Arabian Standard Time", "Абу-Дабі, Маскат"},
        {"Azerbaijan Standard Time", "Баку"},
        {"Mauritius Standard Time", "Порт-Луї"},
        {"Georgian Standard Time", "Тбілісі"},
        {"Caucasus Standard Time", "Єреван"},
        {"Afghanistan Standard Time", "Кабул"},
        {"Ekaterinburg Standard Time", "Єкатеринбург"},
        {"Pakistan Standard Time", "Ісламабад, Карачі"},
        {"West Asia Standard Time", "Ташкент"},
        {"India Standard Time", "Ченнаї, Колката, Мумбаї, Нью-Делі"},
        {"Sri Lanka Standard Time", "Шрі-Джаяварденепура-Котте"},
        {"Nepal Standard Time", "Катманду"},
        {"Central Asia Standard Time", "Астана"},
        {"Bangladesh Standard Time", "Дакка"},
        {"N. Central Asia Standard Time", "Новосибірськ"},
        {"Myanmar Standard Time", "Янгон (Рангун)"},
        {"SE Asia Standard Time", "Бангкок, Ханой, Джакарта"},
        {"North Asia Standard Time", "Красноярськ"},
        {"China Standard Time", "Пекін, Чунцин, Гонконг, Урумчі"},
        {"North Asia East Standard Time", "Іркутськ"},
        {"Singapore Standard Time", "Куала-Лумпур, Сінгапур"},
        {"W. Australia Standard Time", "Перт"},
        {"Taipei Standard Time", "Тайбей"},
        {"Ulaanbaatar Standard Time", "Улан-Батор"},
        {"Tokyo Standard Time", "Осака, Саппоро, Токіо"},
        {"Korea Standard Time", "Сеул"},
        {"Yakutsk Standard Time", "Якутськ"},
        {"Cen. Australia Standard Time", "Аделаїда"},
        {"AUS Central Standard Time", "Дарвін"},
        {"E. Australia Standard Time", "Брисбен"},
        {"AUS Eastern Standard Time", "Канберра, Мельбурн, Сідней"},
        {"West Pacific Standard Time", "Гуам, Порт-Морсбі"},
        {"Tasmania Standard Time", "Хобарт"},
        {"Vladivostok Standard Time", "Владивосток"},
        {"Magadan Standard Time", "Магадан"},
        {"Central Pacific Standard Time", "Соломонові Острови, Нова Каледонія"},
        {"New Zealand Standard Time", "Окленд, Веллінгтон"},
        {"UTC+12", "Всесвітній координований час +12"},
        {"Fiji Standard Time", "Фіджі"},
        {"Tonga Standard Time", "Нукуалофа"},
        {"Samoa Standard Time", "Самоа"},
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
        sorted.append({off, {iana, offsetText(off) + QLatin1Char(' ') + QString::fromUtf8(e.second)}});
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
        return QStringLiteral("У цьому часовому поясі перехід на літній час не використовується.");
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QTimeZone::OffsetData next = tz.nextTransition(now);
    if (!next.atUtc.isValid() || next.atUtc > now.addYears(1)) {
        return QStringLiteral("У цьому часовому поясі перехід на літній час не використовується.");
    }
    const int before = tz.offsetFromUtc(next.atUtc.addSecs(-1));
    const int after = next.offsetFromUtc;
    const QDateTime local = next.atUtc.addSecs(before).toUTC();
    const int hours = qAbs(after - before) / 3600;
    const bool winter = after < before;
    return QStringLiteral("Перехід на %1 час відбудеться %2 о %3. Годинник буде переведено на %4 %5 %6.")
        .arg(winter ? QStringLiteral("зимовий") : QStringLiteral("літній"), longDate(local.date()), local.time().toString(QStringLiteral("H:mm")))
        .arg(hours)
        .arg(hours == 1 ? QStringLiteral("годину") : QStringLiteral("години"), winter ? QStringLiteral("назад") : QStringLiteral("вперед"));
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
        static QSvgRenderer svg(QDir::homePath() + QStringLiteral("/.local/share/plasma/plasmoids/io.gitgud.wackyideas.digitalclocklite/contents/ui/svgs/clock.svg"));
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
        static const QStringList days{QStringLiteral("Пн"), QStringLiteral("Вт"), QStringLiteral("Ср"), QStringLiteral("Чт"), QStringLiteral("Пт"), QStringLiteral("Сб"), QStringLiteral("Нд")};
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
        if (QMessageBox::question(parent, QStringLiteral("Дата й час"),
                                  QStringLiteral("Увімкнено синхронізацію з сервером часу в Інтернеті. Щоб установити час вручну, її потрібно вимкнути.\n\nВимкнути синхронізацію?"))
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
        QMessageBox::warning(parent, QStringLiteral("Дата й час"), QStringLiteral("Не вдалося змінити час:\n%1").arg(err));
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
        fixedDialog(this, QStringLiteral("Настроювання часу й дати"), QStringLiteral("seven-datetime"));
        setMinimumWidth(450);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(12, 12, 12, 12);
        v->addWidget(new QLabel(QStringLiteral("Установіть дату й час:")));
        v->addSpacing(8);
        auto *row = new QHBoxLayout;
        auto *left = new QVBoxLayout;
        left->addWidget(new QLabel(QStringLiteral("Дата:")));
        m_month = new MonthView(QDate::currentDate());
        left->addWidget(m_month);
        left->addStretch(1);
        row->addLayout(left);
        row->addSpacing(18);
        auto *right = new QVBoxLayout;
        right->addWidget(new QLabel(QStringLiteral("Час:")));
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
        auto *link = new Link(QStringLiteral("Змінення параметрів календаря"), [this] {
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
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
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
        fixedDialog(this, QStringLiteral("Вибір часового поясу"), QStringLiteral("seven-datetime"));
        setFixedWidth(460);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(12, 12, 12, 12);
        v->addWidget(new QLabel(QStringLiteral("Установіть часовий пояс:")));
        v->addSpacing(12);
        v->addWidget(new QLabel(QStringLiteral("Часовий пояс:")));
        m_zone = new QComboBox;
        fillZones(m_zone, QTimeZone::systemTimeZoneId());
        v->addWidget(m_zone);
        v->addSpacing(12);
        auto *now = new QHBoxLayout;
        now->addWidget(new QLabel(QStringLiteral("Поточні дата й час:")));
        now->addSpacing(20);
        m_now = new QLabel;
        now->addWidget(m_now, 1);
        v->addLayout(now);
        v->addSpacing(30);
        auto *btns = new QHBoxLayout;
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
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
        fixedDialog(this, QStringLiteral("Настроювання часу через Інтернет"), QStringLiteral("seven-datetime"));
        setFixedWidth(440);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(12, 12, 12, 12);
        v->addWidget(new QLabel(QStringLiteral("Настройте параметри часу через Інтернет:")));
        v->addSpacing(6);
        m_sync = new QCheckBox(QStringLiteral("Синхронізувати із сервером часу в Інтернеті"));
        m_sync->setChecked(ntpActive());
        v->addWidget(m_sync);
        auto *row = new QHBoxLayout;
        row->setContentsMargins(18, 0, 0, 0);
        row->addWidget(new QLabel(QStringLiteral("Сервер:")));
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
        auto *now = new QPushButton(QStringLiteral("Оновити зараз"));
        row->addWidget(now);
        v->addLayout(row);
        v->addSpacing(10);
        m_status = new QLabel(QStringLiteral("Цей комп'ютер налаштовано на автоматичну синхронізацію за розкладом."));
        m_status->setWordWrap(true);
        v->addWidget(m_status);
        v->addSpacing(28);
        auto *btns = new QHBoxLayout;
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
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
            m_status->setText(QStringLiteral("Синхронізація з %1...").arg(m_server->currentText()));
            QTimer::singleShot(3000, this, [this] {
                const bool ok = run(QStringLiteral("timedatectl"), {QStringLiteral("show"), QStringLiteral("-p"), QStringLiteral("NTPSynchronized"), QStringLiteral("--value")}) == QLatin1String("yes");
                const QDateTime t = QDateTime::currentDateTime();
                m_status->setText(ok ? QStringLiteral("Час успішно синхронізовано з %1 %2 о %3.").arg(m_server->currentText(), longDate(t.date()), t.time().toString(QStringLiteral("H:mm")))
                                     : QStringLiteral("Під час синхронізації з %1 сталася помилка.").arg(m_server->currentText()));
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
    QList<ExtraClock> list{{false, QTimeZone::systemTimeZoneId(), QStringLiteral("Годинник 1")}, {false, QTimeZone::systemTimeZoneId(), QStringLiteral("Годинник 2")}};
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
        fixedDialog(this, QStringLiteral("Дата й час"), QStringLiteral("seven-datetime"));
        setAttribute(Qt::WA_DeleteOnClose);
        setFixedWidth(480);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 9, 7, 9);
        auto *tabs = new QTabWidget;
        tabs->addTab(dateTab(), QStringLiteral("Дата й час"));
        tabs->addTab(clocksTab(), QStringLiteral("Додаткові годинники"));
        tabs->addTab(internetTab(), QStringLiteral("Час в Інтернеті"));
        tabs->setCurrentIndex(tab);
        v->addWidget(tabs, 1);
        auto *btns = new QHBoxLayout;
        btns->setContentsMargins(0, 4, 0, 0);
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
        m_apply = new QPushButton(QStringLiteral("Застосувати"));
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
        info->addWidget(new QLabel(QStringLiteral("Дата:")));
        m_date = new QLabel;
        info->addWidget(m_date);
        info->addSpacing(16);
        info->addWidget(new QLabel(QStringLiteral("Час:")));
        m_time = new QLabel;
        info->addWidget(m_time);
        info->addSpacing(10);
        auto *change = shieldButton(QStringLiteral("Змінити дату й час..."));
        info->addWidget(change, 0, Qt::AlignRight);
        top->addLayout(info, 1);
        v->addLayout(top);
        v->addSpacing(8);
        auto *zoneHead = new QHBoxLayout;
        zoneHead->addWidget(new QLabel(QStringLiteral("Часовий пояс")));
        auto *line = new QFrame;
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet(QStringLiteral("color: #d5dfe5;"));
        zoneHead->addWidget(line, 1);
        v->addLayout(zoneHead);
        v->addSpacing(6);
        m_zone = new QLabel;
        v->addWidget(m_zone);
        v->addSpacing(6);
        auto *changeZone = new QPushButton(QStringLiteral("Змінити часовий пояс..."));
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
        v->addWidget(text(QStringLiteral("Додаткові годинники можуть показувати час в інших часових поясах. Щоб переглянути їх, клацніть годинник на панелі завдань "
                                         "або наведіть на нього вказівник миші.")));
        m_oldClocks = readExtraClocks();
        for (int i = 0; i < 2; ++i) {
            auto *box = new QGroupBox;
            auto *bv = new QVBoxLayout(box);
            bv->setSpacing(4);
            auto *show = new QCheckBox(QStringLiteral("Показати цей годинник"));
            show->setChecked(m_oldClocks[i].show);
            auto *zoneLabel = new QLabel(QStringLiteral("Виберіть часовий пояс:"));
            auto *zone = new QComboBox;
            fillZones(zone, m_oldClocks[i].zone);
            auto *nameLabel = new QLabel(QStringLiteral("Відображуване ім'я:"));
            auto *name = new QLineEdit(m_oldClocks[i].name.isEmpty() ? QStringLiteral("Годинник %1").arg(i + 1) : m_oldClocks[i].name);
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
        auto *change = shieldButton(QStringLiteral("Змінити параметри..."));
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
            m_internet->setText(QStringLiteral("Комп'ютер налаштовано на автоматичну синхронізацію з «%1».").arg(timeServer()));
            m_internet2->setText(QStringLiteral("Цей комп'ютер налаштовано на автоматичну синхронізацію за розкладом."));
        } else {
            m_internet->setText(QStringLiteral("Цей комп'ютер не налаштовано на автоматичну синхронізацію."));
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
