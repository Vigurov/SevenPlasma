#include "win7i18n.h"
/*
 * Currency: Currency.Gadget (currency.js / currency.css) — the gold card converting between two to four currencies.
 * Rates: ExchangeRate-API's open endpoint (open.er-api.com, daily, no key). Included inside main.cpp's namespace.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

QString currencyName(const QString &code)
{
    static const QHash<QString, QString> names{
        {QStringLiteral("UAH"), tr7("Ukrainian Hryvnia")}, {QStringLiteral("USD"), tr7("US Dollar")},
        {QStringLiteral("EUR"), tr7("Euro")}, {QStringLiteral("GBP"), tr7("British Pound")},
        {QStringLiteral("PLN"), tr7("Polish Zloty")}, {QStringLiteral("CHF"), tr7("Swiss Franc")},
        {QStringLiteral("JPY"), tr7("Japanese Yen")}, {QStringLiteral("CNY"), tr7("Chinese Yuan")},
        {QStringLiteral("CZK"), tr7("Czech Koruna")}, {QStringLiteral("CAD"), tr7("Canadian Dollar")},
        {QStringLiteral("AUD"), tr7("Australian Dollar")}, {QStringLiteral("SEK"), tr7("Swedish Krona")},
        {QStringLiteral("NOK"), tr7("Norwegian Krone")}, {QStringLiteral("DKK"), tr7("Danish Krone")},
        {QStringLiteral("HUF"), tr7("Hungarian Forint")}, {QStringLiteral("RON"), tr7("Romanian Leu")},
        {QStringLiteral("MDL"), tr7("Moldovan Leu")}, {QStringLiteral("TRY"), tr7("Turkish Lira")},
        {QStringLiteral("GEL"), tr7("Georgian Lari")}, {QStringLiteral("KZT"), tr7("Kazakhstani Tenge")},
        {QStringLiteral("ILS"), tr7("Israeli Shekel")}, {QStringLiteral("INR"), tr7("Indian Rupee")},
        {QStringLiteral("BGN"), tr7("Bulgarian Lev")}, {QStringLiteral("AZN"), tr7("Azerbaijani Manat")},
        {QStringLiteral("AMD"), tr7("Armenian Dram")}, {QStringLiteral("KRW"), tr7("South Korean Won")},
        {QStringLiteral("SGD"), tr7("Singapore Dollar")}, {QStringLiteral("HKD"), tr7("Hong Kong Dollar")},
        {QStringLiteral("NZD"), tr7("New Zealand Dollar")}, {QStringLiteral("MXN"), tr7("Mexican Peso")},
        {QStringLiteral("BRL"), tr7("Brazilian Real")}, {QStringLiteral("ZAR"), tr7("South African Rand")},
        {QStringLiteral("AED"), tr7("UAE Dirham")}, {QStringLiteral("SAR"), tr7("Saudi Riyal")},
        {QStringLiteral("EGP"), tr7("Egyptian Pound")}, {QStringLiteral("THB"), tr7("Thai Baht")},
        {QStringLiteral("ISK"), tr7("Icelandic Krona")}, {QStringLiteral("RSD"), tr7("Serbian Dinar")},
        {QStringLiteral("BYN"), tr7("Belarusian Ruble")}, {QStringLiteral("RUB"), tr7("Russian Ruble")},
        {QStringLiteral("IDR"), tr7("Indonesian Rupiah")},
        {QStringLiteral("MYR"), tr7("Malaysian Ringgit")}, {QStringLiteral("PHP"), tr7("Philippine Peso")},
        {QStringLiteral("VND"), tr7("Vietnamese Dong")}, {QStringLiteral("ARS"), tr7("Argentine Peso")},
        {QStringLiteral("CLP"), tr7("Chilean Peso")}, {QStringLiteral("COP"), tr7("Colombian Peso")},
        {QStringLiteral("PEN"), tr7("Peruvian Sol")}, {QStringLiteral("UZS"), tr7("Uzbekistani Som")},
        {QStringLiteral("TWD"), tr7("New Taiwan Dollar")}, {QStringLiteral("QAR"), tr7("Qatari Riyal")},
        {QStringLiteral("KWD"), tr7("Kuwaiti Dinar")}, {QStringLiteral("MAD"), tr7("Moroccan Dirham")},
        {QStringLiteral("XAU"), tr7("Gold (ounce)")},
    };
    return names.value(code, code);
}

/** Rates per 1 USD, shared by all Currency gadgets; refetched every few hours */
class CurrencyRates : public QObject
{
public:
    static CurrencyRates &instance()
    {
        static CurrencyRates r;
        return r;
    }

    QHash<QString, double> rates;
    QDateTime updated; // when the provider published them
    bool failed = false;
    QList<std::function<void()>> listeners;

    void refresh()
    {
        if (m_busy) {
            return;
        }
        m_busy = true;
        QNetworkRequest req(QUrl(QStringLiteral("https://open.er-api.com/v6/latest/USD")));
        req.setTransferTimeout(20000);
        QNetworkReply *reply = net().get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            reply->deleteLater();
            m_busy = false;
            const QByteArray data = reply->readAll();
            const QJsonObject o = QJsonDocument::fromJson(data).object();
            if (reply->error() != QNetworkReply::NoError || o.value(QStringLiteral("result")).toString() != QLatin1String("success")) {
                failed = rates.isEmpty();
                notify();
                return;
            }
            parse(o);
            failed = false;
            QDir().mkpath(QFileInfo(cacheFile()).path());
            QFile f(cacheFile());
            if (f.open(QIODevice::WriteOnly)) {
                f.write(data);
            }
            notify();
        });
    }

private:
    CurrencyRates()
    {
        // the last rates we saw, so the card works offline
        QFile f(cacheFile());
        const QJsonObject o = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
        if (!o.isEmpty()) {
            parse(o);
        }
        m_timer.setInterval(3 * 3600 * 1000);
        connect(&m_timer, &QTimer::timeout, this, &CurrencyRates::refresh);
        m_timer.start();
        QTimer::singleShot(0, this, &CurrencyRates::refresh);
    }

    /** Not in seven-gadgetsrc: KConfig shares that file and dislikes JSON in it */
    static QString cacheFile()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/seven-gadgets/rates.json");
    }

    void parse(const QJsonObject &o)
    {
        rates.clear();
        const QJsonObject r = o.value(QStringLiteral("rates")).toObject();
        for (auto it = r.begin(); it != r.end(); ++it) {
            rates.insert(it.key(), it.value().toDouble());
        }
        updated = QDateTime::fromSecsSinceEpoch(qint64(o.value(QStringLiteral("time_last_update_unix")).toDouble()));
    }

    void notify()
    {
        for (const auto &f : std::as_const(listeners)) {
            f();
        }
    }

    bool m_busy = false;
    QTimer m_timer;
};

class CurrencyGadget : public Gadget
{
public:
    explicit CurrencyGadget(const QString &gadgetId)
        : Gadget(gadgetId)
    {
        m_codes = store().value(id + QStringLiteral("/Currencies"), QStringList{QStringLiteral("USD"), QStringLiteral("UAH")}).toStringList();
        while (m_codes.size() < 2) {
            m_codes << QStringLiteral("EUR");
        }
        m_codes = m_codes.mid(0, 4);
        m_baseRow = 0;
        m_baseAmount = store().value(id + QStringLiteral("/Amount"), 1.0).toDouble();
        for (int i = 0; i < 4; ++i) {
            auto *edit = new QLineEdit(this);
            edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            edit->setFrame(false);
            edit->setFont(gadgetFont(12, true));
            edit->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none; color: black; padding: 0 3px 0 1px; }"));
            connect(edit, &QLineEdit::textEdited, this, [this, i](const QString &text) {
                bool ok = false;
                const double v = QLocale{}.toDouble(QString(text).replace(QLatin1Char('.'), QLatin1Char(',')).remove(QChar(0xa0)).remove(QLatin1Char(' ')), &ok);
                if (!ok) {
                    return;
                }
                m_baseRow = i;
                m_baseAmount = v;
                recalc(i);
            });
            connect(edit, &QLineEdit::editingFinished, this, [this] {
                recalc(-1);
                save();
            });
            m_edits << edit;
        }
        QPointer<CurrencyGadget> self(this);
        CurrencyRates::instance().listeners << [self] {
            if (self) {
                self->recalc(-1);
                self->update();
            }
        };
        connect(&m_age, &QTimer::timeout, this, [this] {
            update();
        });
        m_age.start(60 * 1000);
        layoutEdits();
        recalc(-1);
    }

protected:
    QSize bodySize() const override
    {
        if (!isBig()) {
            return QSize(130, 83);
        }
        return QSize(254, 171 + 55 * (int(m_codes.size()) - 2));
    }
    bool glassFrame() const override
    {
        return false;
    }
    bool hasSettingsButton() const override
    {
        return false;
    }
    void configure() override
    {
    }

    int rows() const
    {
        return isBig() ? int(m_codes.size()) : 2;
    }
    /** The white value box of a row */
    QRect boxRect(int row) const
    {
        if (!isBig()) {
            return QRect(130 - 7 - 72, 9 + row * 30, 72, 21);
        }
        return QRect(13 + 19, 15 + row * 55 + 25, 180, 21);
    }
    QRect titleRect(int row) const
    {
        if (!isBig()) {
            return QRect(7, 9 + row * 30, 40, 21);
        }
        return QRect(13 + 19, 15 + row * 55 + 3, 180, 18);
    }
    QRect deleteRect(int row) const
    {
        const QRect b = boxRect(row);
        return QRect(b.right() + 5, b.top() + 2, 16, 16);
    }
    QRect addRect() const
    {
        return QRect(254 - 28 - 16 + 4, height() - 22 - 16, 16, 16);
    }

    void paintBody(QPainter &p, const QRectF &) override
    {
        const bool big = isBig();
        p.drawPixmap(0, 0, gadgetPixmap(big ? QStringLiteral("currency/base-undocked-%1.png").arg(m_codes.size()) : QStringLiteral("currency/base-docked.png")));
        const CurrencyRates &r = CurrencyRates::instance();
        if (r.rates.isEmpty()) {
            p.setFont(gadgetFont(12));
            const QString msg = r.failed ? tr7("Could not contact the data provider.") : tr7("Loading...");
            drawShadowText(p, QRectF(rect()).adjusted(10, 6, -10, -6), Qt::AlignCenter | Qt::TextWordWrap, msg, Qt::white);
            return;
        }
        const QPoint cursor = mapFromGlobal(QCursor::pos());
        for (int i = 0; i < rows(); ++i) {
            if (big && underMouse() && QRect(14, 15 + i * 55, 218, 49).contains(cursor)) {
                p.drawPixmap(14, 15 + i * 55, gadgetPixmap(QStringLiteral("currency/row_over.png")));
            }
            // currencyValueBack: white at 50 % with a black line
            const QRect b = boxRect(i);
            p.fillRect(b, QColor(255, 255, 255, 128));
            p.setPen(QColor(0, 0, 0, 128));
            p.drawRect(b.adjusted(0, 0, -1, -1));
            const QString code = m_codes.at(i);
            if (big) {
                p.setFont(gadgetFont(12));
                const QString name = currencyName(code);
                const QFontMetrics fm(p.font());
                const QString shown = fm.elidedText(name, Qt::ElideRight, titleRect(i).width() - 14);
                drawShadowText(p, titleRect(i), Qt::AlignLeft | Qt::AlignVCenter, shown, Qt::white);
                const QPixmap &tri = gadgetPixmap(QStringLiteral("currency/triangle.png"));
                p.drawPixmap(titleRect(i).left() + fm.horizontalAdvance(shown) + 4, titleRect(i).center().y() - 1, tri);
                if (m_codes.size() > 2 && underMouse() && QRect(14, 15 + i * 55, 230, 49).contains(cursor)) {
                    const bool hot = deleteRect(i).contains(cursor);
                    p.drawPixmap(deleteRect(i).topLeft(), gadgetPixmap(hot ? QStringLiteral("currency/delete_over.png") : QStringLiteral("currency/delete_up.png")));
                }
            } else {
                p.setFont(gadgetFont(11, true));
                drawShadowText(p, titleRect(i), Qt::AlignLeft | Qt::AlignVCenter, code, Qt::white);
            }
        }
        p.setFont(gadgetFont(11));
        if (big) {
            p.setFont(gadgetFont(10));
            drawShadowText(p, QRect(20, height() - 22 - 16, 100, 16), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("ExchangeRate-API"), Qt::white);
            p.setFont(gadgetFont(11));
            drawShadowText(p, QRect(120, height() - 22 - 16, 95, 16), Qt::AlignRight | Qt::AlignVCenter, ageText(), Qt::white);
            if (m_codes.size() < 4) {
                const bool hot = underMouse() && addRect().contains(cursor);
                p.drawPixmap(addRect().topLeft(), gadgetPixmap(hot ? QStringLiteral("currency/add_over.png") : QStringLiteral("currency/add_up.png")));
            }
        } else {
            drawShadowText(p, QRect(7, 83 - 5 - 15, 130 - 14, 15), Qt::AlignRight | Qt::AlignVCenter, ageText(), Qt::white);
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        Gadget::mouseMoveEvent(e);
        if (isBig()) {
            update();
        }
    }
    void bodyClicked(QPointF pos) override
    {
        const QPoint at = pos.toPoint();
        if (isBig() && m_codes.size() < 4 && addRect().contains(at)) {
            static const QStringList extra{QStringLiteral("EUR"), QStringLiteral("PLN"), QStringLiteral("GBP"), QStringLiteral("USD"), QStringLiteral("UAH")};
            for (const QString &c : extra) {
                if (!m_codes.contains(c)) {
                    m_codes << c;
                    break;
                }
            }
            sizeChanged();
            return;
        }
        for (int i = 0; i < rows(); ++i) {
            if (isBig() && m_codes.size() > 2 && deleteRect(i).contains(at)) {
                m_codes.removeAt(i);
                if (m_baseRow >= m_codes.size()) {
                    m_baseRow = 0;
                }
                sizeChanged();
                return;
            }
            if (titleRect(i).contains(at)) {
                chooseCurrency(i);
                return;
            }
        }
    }
    void resizeEvent(QResizeEvent *e) override
    {
        Gadget::resizeEvent(e);
        layoutEdits();
    }

private:
    void sizeChanged()
    {
        save();
        resizeToContent();
        layoutEdits();
        recalc(-1);
        update();
    }

    void layoutEdits()
    {
        for (int i = 0; i < m_edits.size(); ++i) {
            m_edits.at(i)->setVisible(i < rows());
            if (i < rows()) {
                m_edits.at(i)->setGeometry(boxRect(i).adjusted(1, 1, -1, -1));
            }
        }
    }

    void chooseCurrency(int row)
    {
        QStringList codes = CurrencyRates::instance().rates.keys();
        QCollator c(QLocale{});
        std::sort(codes.begin(), codes.end(), [&c](const QString &a, const QString &b) {
            return c.compare(currencyName(a), currencyName(b)) < 0;
        });
        QMenu menu;
        menu.setStyleSheet(QStringLiteral("QMenu { menu-scrollable: 1; }"));
        // the usual ones first
        for (const QString &code : {QStringLiteral("UAH"), QStringLiteral("USD"), QStringLiteral("EUR"), QStringLiteral("PLN"), QStringLiteral("GBP")}) {
            QAction *a = menu.addAction(QStringLiteral("%1 (%2)").arg(currencyName(code), code));
            a->setData(code);
            a->setCheckable(true);
            a->setChecked(code == m_codes.at(row));
        }
        menu.addSeparator();
        QMenu *all = menu.addMenu(tr7("All currencies"));
        for (const QString &code : std::as_const(codes)) {
            if (currencyName(code) == code) {
                continue; // no Ukrainian name: rarely wanted
            }
            QAction *a = all->addAction(QStringLiteral("%1 (%2)").arg(currencyName(code), code));
            a->setData(code);
        }
        QAction *picked = menu.exec(mapToGlobal(titleRect(row).bottomLeft()));
        if (!picked || picked->data().toString().isEmpty()) {
            return;
        }
        m_codes[row] = picked->data().toString();
        save();
        recalc(-1);
        update();
    }

    /** Fills every box from the row the user typed in (skip: leave that one alone while typing) */
    void recalc(int skip)
    {
        const auto &rates = CurrencyRates::instance().rates;
        const double base = rates.value(m_codes.value(m_baseRow), 0);
        QStringList tips;
        for (int i = 0; i < m_codes.size() && i < m_edits.size(); ++i) {
            const double rate = rates.value(m_codes.at(i), 0);
            const double v = base > 0 ? m_baseAmount * rate / base : 0;
            if (i != skip) {
                m_edits.at(i)->setText(rates.isEmpty() ? QString() : (i == m_baseRow ? formatAmount(m_baseAmount) : formatAmount(v)));
            }
            tips << QStringLiteral("%1 %2").arg(formatAmount(v), currencyName(m_codes.at(i)));
            m_edits.at(i)->setToolTip(currencyName(m_codes.at(i)));
        }
        setToolTip(tips.join(QStringLiteral(" = ")));
    }

    static QString formatAmount(double v)
    {
        const QLocale l(QLocale::Ukrainian);
        const double a = std::abs(v);
        const int decimals = a == 0 ? 0 : (a >= 1 ? 2 : qMin(6, 2 - int(std::floor(std::log10(a)))));
        return l.toString(v, 'f', decimals);
    }

    QString ageText() const
    {
        const QDateTime t = CurrencyRates::instance().updated;
        if (!t.isValid()) {
            return {};
        }
        const qint64 mins = t.secsTo(QDateTime::currentDateTime()) / 60;
        if (mins < 1) {
            return tr7("Just now");
        }
        if (mins < 60) {
            return tr7("%1 min ago").arg(mins);
        }
        if (mins < 24 * 60) {
            return tr7("%1 hours ago").arg(mins / 60);
        }
        return t.toString(QStringLiteral("dd.MM.yyyy"));
    }

    void save()
    {
        // remember the amount in the first currency
        const auto &rates = CurrencyRates::instance().rates;
        const double base = rates.value(m_codes.value(m_baseRow), 0);
        const double first = rates.value(m_codes.value(0), 0);
        if (base > 0 && first > 0) {
            m_baseAmount = m_baseAmount * first / base;
            m_baseRow = 0;
        }
        store().setValue(id + QStringLiteral("/Currencies"), m_codes);
        store().setValue(id + QStringLiteral("/Amount"), m_baseAmount);
        store().sync();
    }

    QStringList m_codes;
    int m_baseRow = 0;
    double m_baseAmount = 1;
    QList<QLineEdit *> m_edits;
    QTimer m_age;
};
