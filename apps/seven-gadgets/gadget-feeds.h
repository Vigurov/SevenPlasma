#include "win7i18n.h"
/*
 * Feed Headlines: RSSFeeds.Gadget (RSSFeeds.js / RSSFeeds.css / flyout.css) — four headlines at a time,
 * the up/down pager on hover and the flyout with the article. Windows 7 read the feeds IE subscribed to;
 * here the gadget keeps its own list of RSS / Atom addresses. Included inside main.cpp's namespace.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

struct FeedItem {
    QString title;
    QString link;
    QString feed;
    QString description;
    QDateTime date;
};

QStringList defaultFeeds()
{
    return {QStringLiteral("https://feeds.bbci.co.uk/ukrainian/rss.xml"), QStringLiteral("https://www.ukrinform.ua/rss/block-lastnews")};
}

/** RSS 2.0 <item> or Atom <entry> */
QList<FeedItem> parseFeed(const QByteArray &data)
{
    QList<FeedItem> items;
    QXmlStreamReader xml(data);
    QString feedTitle;
    FeedItem cur;
    bool inItem = false;
    while (!xml.atEnd()) {
        xml.readNext();
        const QStringView name = xml.name();
        if (xml.isStartElement()) {
            if (name == u"item" || name == u"entry") {
                inItem = true;
                cur = FeedItem();
            } else if (name == u"title") {
                const QString t = xml.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
                if (inItem) {
                    cur.title = t;
                } else if (feedTitle.isEmpty()) {
                    feedTitle = t;
                }
            } else if (inItem && name == u"link") {
                const QString href = xml.attributes().value(QStringLiteral("href")).toString();
                const QString rel = xml.attributes().value(QStringLiteral("rel")).toString();
                const QString text = xml.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
                if (!href.isEmpty() && (rel.isEmpty() || rel == QLatin1String("alternate"))) {
                    cur.link = href;
                } else if (!text.isEmpty() && cur.link.isEmpty()) {
                    cur.link = text;
                }
            } else if (inItem && (name == u"description" || name == u"summary" || (name == u"content" && cur.description.isEmpty()))) {
                cur.description = QTextDocumentFragment::fromHtml(xml.readElementText(QXmlStreamReader::IncludeChildElements)).toPlainText().simplified();
            } else if (inItem && (name == u"pubDate" || name == u"published" || name == u"updated" || name == u"date")) {
                const QString t = xml.readElementText().trimmed();
                QDateTime d = QDateTime::fromString(t, Qt::RFC2822Date);
                if (!d.isValid()) {
                    d = QDateTime::fromString(t, Qt::ISODate);
                }
                if (d.isValid() && (!cur.date.isValid() || name == u"pubDate" || name == u"published")) {
                    cur.date = d;
                }
            }
        } else if (xml.isEndElement() && (name == u"item" || name == u"entry")) {
            inItem = false;
            if (!cur.title.isEmpty()) {
                items << cur;
            }
        }
    }
    for (FeedItem &i : items) {
        i.feed = feedTitle;
    }
    return items;
}

class FeedsSettings : public GadgetDialog
{
public:
    FeedsSettings(const QStringList &feeds, int count)
        : GadgetDialog(tr7("Feed Headlines"), 380)
    {
        body->addWidget(new QLabel(tr7("Feeds (RSS or Atom):")));
        m_list = new QListWidget;
        m_list->addItems(feeds);
        m_list->setMinimumHeight(110);
        body->addWidget(m_list);
        auto *row = new QHBoxLayout;
        m_url = new QLineEdit;
        m_url->setPlaceholderText(QStringLiteral("https://..."));
        auto *add = new QPushButton(tr7("Add"));
        auto *remove = new QPushButton(tr7("Delete"));
        row->addWidget(m_url, 1);
        row->addWidget(add);
        row->addWidget(remove);
        body->addLayout(row);
        body->addSpacing(8);
        body->addWidget(new QLabel(tr7("Show the latest:")));
        m_count = new QComboBox;
        for (int n : {10, 20, 40, 100}) {
            m_count->addItem(tr7("%1 headlines").arg(n), n);
        }
        m_count->setCurrentIndex(qMax(0, m_count->findData(count)));
        body->addWidget(m_count);
        body->addStretch(1);
        connect(add, &QPushButton::clicked, this, [this] {
            const QUrl u = QUrl::fromUserInput(m_url->text().trimmed());
            if (u.isValid() && !m_url->text().trimmed().isEmpty()) {
                m_list->addItem(u.toString());
                m_url->clear();
            }
        });
        connect(m_url, &QLineEdit::returnPressed, add, &QPushButton::click);
        connect(remove, &QPushButton::clicked, this, [this] {
            delete m_list->currentItem();
        });
    }

    QStringList feeds() const
    {
        QStringList l;
        for (int i = 0; i < m_list->count(); ++i) {
            l << m_list->item(i)->text();
        }
        return l;
    }
    int count() const
    {
        return m_count->currentData().toInt();
    }

private:
    QListWidget *m_list;
    QLineEdit *m_url;
    QComboBox *m_count;
};

/** flyout.html: the article title, its feed and date, "Читати в Інтернеті..." and the description */
class FeedFlyout : public QWidget
{
public:
    std::function<void()> closed;

    FeedFlyout()
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        setFixedSize(303, 225);
        m_text = new QLabel(this);
        m_text->setGeometry(12, 40, 279, 172);
        m_text->setWordWrap(true);
        m_text->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_text->setTextFormat(Qt::RichText);
        m_text->setOpenExternalLinks(true);
        m_text->setFont(gadgetFont(12));
        m_text->setStyleSheet(QStringLiteral("QLabel { color: white; background: transparent; }"));
    }

    void setItem(const FeedItem &item)
    {
        m_item = item;
        m_text->setText(tr7("<a style='color:#ffffff; text-decoration:none' href='%1'><b>Read online...</b></a><br><br>%2")
                            .arg(item.link.toHtmlEscaped(), item.description.toHtmlEscaped()));
        update();
    }

protected:
    QRect titleRect() const
    {
        return QRect(10, 3, 286, 19);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.drawPixmap(0, 0, gadgetPixmap(QStringLiteral("feeds/flyoutBack.png")));
        p.setFont(gadgetFont(14, true));
        const bool hot = titleRect().contains(mapFromGlobal(QCursor::pos()));
        p.setPen(hot ? QColor(0xd6, 0xf4, 0xff) : Qt::white);
        p.drawText(titleRect(), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(p.font()).elidedText(m_item.title, Qt::ElideRight, titleRect().width()));
        p.setFont(gadgetFont(11));
        p.setPen(Qt::white);
        QString sub = m_item.feed;
        if (m_item.date.isValid()) {
            sub += QStringLiteral(", ") + QLocale{}.toString(m_item.date.toLocalTime(), QStringLiteral("d MMMM yyyy, HH:mm"));
        }
        p.drawText(QRect(10, 19, 280, 18), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(p.font()).elidedText(sub, Qt::ElideRight, 280));
    }
    void mouseMoveEvent(QMouseEvent *) override
    {
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (titleRect().contains(e->position().toPoint())) {
            QDesktopServices::openUrl(QUrl(m_item.link));
        }
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Escape) {
            close();
        }
    }
    void changeEvent(QEvent *e) override
    {
        // like a flyout: gone as soon as something else is clicked
        if (e->type() == QEvent::ActivationChange && !isActiveWindow() && isVisible()) {
            QTimer::singleShot(0, this, &QWidget::close);
        }
        QWidget::changeEvent(e);
    }
    void closeEvent(QCloseEvent *e) override
    {
        if (closed) {
            closed();
        }
        QWidget::closeEvent(e);
    }

private:
    FeedItem m_item;
    QLabel *m_text;
};

class FeedsGadget : public Gadget
{
public:
    explicit FeedsGadget(const QString &gadgetId)
        : Gadget(gadgetId)
    {
        setMouseTracking(true);
        loadSettings();
        connect(&m_rotate, &QTimer::timeout, this, [this] {
            // setNextViewItems() every 10 s while the mouse is away
            if (!underMouse() && !m_flyout && !m_items.isEmpty()) {
                step(1);
            }
        });
        m_rotate.start(10000);
        connect(&m_poll, &QTimer::timeout, this, &FeedsGadget::fetch);
        m_poll.start(30 * 60 * 1000);
        fetch();
    }
    ~FeedsGadget() override
    {
        if (m_flyout) {
            m_flyout->closed = nullptr;
            m_flyout->close();
        }
    }

protected:
    static constexpr int kView = 4; // g_countToView

    QSize bodySize() const override
    {
        return isBig() ? QSize(296, 232) : QSize(130, 173);
    }
    bool glassFrame() const override
    {
        return false;
    }
    QRect itemRect(int i) const
    {
        return isBig() ? QRect(16, 14 + i * 44, 264, 43) : QRect(4, 6 + i * 35, 122, 34);
    }
    QPoint navPos() const
    {
        return isBig() ? QPoint(109, 190) : QPoint(26, 147);
    }
    QRect upRect() const
    {
        return QRect(navPos() + QPoint(1, 2), QSize(16, 16));
    }
    QRect downRect() const
    {
        return QRect(navPos() + QPoint(61, 2), QSize(16, 16));
    }

    void paintBody(QPainter &p, const QRectF &) override
    {
        const bool big = isBig();
        p.drawPixmap(0, 0, gadgetPixmap(big ? QStringLiteral("feeds/rssBackBlue_Undocked.png") : QStringLiteral("feeds/rssBackBlue_docked.png")));
        p.setRenderHint(QPainter::TextAntialiasing);
        if (m_items.isEmpty()) {
            p.setFont(gadgetFont(12));
            QString msg = m_loading ? tr7("Loading...") : tr7("No headlines to display.");
            if (m_feeds.isEmpty()) {
                msg = tr7("This gadget shows the feeds you add in its options.");
            }
            p.setPen(Qt::white);
            p.drawText(QRect(10, int(height() * 0.3), width() - 20, height() / 2), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, msg);
            return;
        }
        const QPoint cursor = mapFromGlobal(QCursor::pos());
        const QFont titleFont = gadgetFont(big ? 14 : 12);
        QFont unread = titleFont;
        unread.setBold(true);
        const QFont small = gadgetFont(big ? 12 : 11);
        for (int i = 0; i < kView; ++i) {
            const int index = m_first + i;
            if (index >= m_items.size()) {
                break;
            }
            const FeedItem &it = m_items.at(index);
            const QRect r = itemRect(i);
            if (it.link == m_selected) {
                p.drawPixmap(r.topLeft(), gadgetPixmap(big ? QStringLiteral("feeds/rss_headline_glow_floating.png") : QStringLiteral("feeds/rss_headline_glow_docked.png")));
            } else if (underMouse() && r.contains(cursor)) {
                p.drawPixmap(r.topLeft(), gadgetPixmap(big ? QStringLiteral("feeds/item_hover_floating.png") : QStringLiteral("feeds/item_hover_docked.png")));
            }
            const int pad = big ? 7 : 5;
            const bool isRead = m_read.contains(it.link);
            p.setFont(isRead ? titleFont : unread);
            p.setPen(Qt::white);
            const QRect tr(r.left() + pad, r.top() + (big ? 4 : 3), r.width() - 2 * pad, big ? 17 : 14);
            p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(p.font()).elidedText(it.title, Qt::ElideRight, tr.width()));
            p.setFont(small);
            p.setPen(QColor(0x67, 0x78, 0x8a));
            const QRect sr(r.left() + pad, r.top() + (big ? 23 : 18), r.width() - 2 * pad, 14);
            const QString when = dateText(it.date);
            const int whenW = QFontMetrics(small).horizontalAdvance(when);
            p.drawText(sr, Qt::AlignRight | Qt::AlignVCenter, when);
            p.drawText(sr.adjusted(0, 0, -whenW - 6, 0), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(small).elidedText(it.feed, Qt::ElideRight, sr.width() - whenW - 6));
            // dotted 1px #3b4458 under every headline
            if (index < m_items.size() - 1 && i < kView - 1) {
                QPen dots(QColor(0x3b, 0x44, 0x58), 1, Qt::DotLine);
                p.setPen(dots);
                p.drawLine(QPointF(r.left() + 2, r.bottom() + 0.5), QPointF(r.right() - 2, r.bottom() + 0.5));
            }
        }
        if (underMouse()) {
            const QPoint n = navPos();
            p.drawPixmap(n, gadgetPixmap(QStringLiteral("feeds/navBack.png")));
            p.drawPixmap(upRect().topLeft(), gadgetPixmap(upRect().contains(cursor) ? QStringLiteral("feeds/buttonUp_On.png") : QStringLiteral("feeds/buttonUp_Off.png")));
            p.drawPixmap(downRect().topLeft(), gadgetPixmap(downRect().contains(cursor) ? QStringLiteral("feeds/buttonDown_On.png") : QStringLiteral("feeds/buttonDown_Off.png")));
            p.setFont(gadgetFont(12));
            p.setPen(Qt::white);
            const int last = qMin(int(m_items.size()), m_first + kView);
            p.drawText(QRect(n.x() + 17, n.y(), 44, 20), Qt::AlignCenter, QStringLiteral("%1-%2").arg(m_first + 1).arg(last));
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        Gadget::mouseMoveEvent(e);
        update();
    }
    void wheelEvent(QWheelEvent *e) override
    {
        step(e->angleDelta().y() < 0 ? 1 : -1);
    }
    void bodyClicked(QPointF pos) override
    {
        const QPoint at = pos.toPoint();
        if (upRect().contains(at)) {
            step(-1);
            return;
        }
        if (downRect().contains(at)) {
            step(1);
            return;
        }
        for (int i = 0; i < kView; ++i) {
            const int index = m_first + i;
            if (index < m_items.size() && itemRect(i).contains(at)) {
                openFlyout(m_items.at(index));
                return;
            }
        }
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        for (int i = 0; i < kView; ++i) {
            const int index = m_first + i;
            if (index < m_items.size() && itemRect(i).contains(e->position().toPoint())) {
                QDesktopServices::openUrl(QUrl(m_items.at(index).link));
                return;
            }
        }
    }

    void configure() override
    {
        FeedsSettings dlg(m_feeds, m_count);
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        store().setValue(id + QStringLiteral("/Feeds"), dlg.feeds());
        store().setValue(id + QStringLiteral("/Count"), dlg.count());
        store().sync();
        loadSettings();
        fetch();
    }

private:
    void loadSettings()
    {
        m_feeds = store().value(id + QStringLiteral("/Feeds"), defaultFeeds()).toStringList();
        m_count = store().value(id + QStringLiteral("/Count"), 20).toInt();
        const QStringList read = store().value(QStringLiteral("Feeds/Read")).toStringList();
        m_read = QSet<QString>(read.begin(), read.end());
    }

    void fetch()
    {
        m_pending = int(m_feeds.size());
        m_loading = m_pending > 0;
        m_incoming.clear();
        update();
        for (const QString &url : std::as_const(m_feeds)) {
            QNetworkRequest req{QUrl(url)};
            req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) seven-gadgets"));
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            req.setTransferTimeout(30000);
            QNetworkReply *reply = net().get(req);
            QPointer<FeedsGadget> self(this);
            connect(reply, &QNetworkReply::finished, this, [self, reply] {
                reply->deleteLater();
                if (!self) {
                    return;
                }
                if (reply->error() == QNetworkReply::NoError) {
                    self->m_incoming += parseFeed(reply->readAll());
                }
                if (--self->m_pending <= 0) {
                    self->finish();
                }
            });
        }
    }

    void finish()
    {
        m_loading = false;
        std::stable_sort(m_incoming.begin(), m_incoming.end(), [](const FeedItem &a, const FeedItem &b) {
            return a.date > b.date;
        });
        if (!m_incoming.isEmpty()) {
            m_items = m_incoming.mid(0, m_count);
        }
        m_incoming.clear();
        m_first = qMin(m_first, qMax(0, int(m_items.size()) - 1));
        m_first -= m_first % kView;
        update();
    }

    void step(int d)
    {
        if (m_items.isEmpty()) {
            return;
        }
        const int pages = (int(m_items.size()) + kView - 1) / kView;
        const int page = ((m_first / kView + d) % pages + pages) % pages;
        m_first = page * kView;
        update();
    }

    void openFlyout(const FeedItem &item)
    {
        m_selected = item.link;
        m_read.insert(item.link);
        QStringList read = store().value(QStringLiteral("Feeds/Read")).toStringList();
        if (!read.contains(item.link)) {
            read << item.link;
        }
        store().setValue(QStringLiteral("Feeds/Read"), read.mid(qMax(0, int(read.size()) - 500)));
        if (!m_flyout) {
            m_flyout = new FeedFlyout;
            m_flyout->closed = [this] {
                m_selected.clear();
                update();
            };
        }
        m_flyout->setItem(item);
        // beside the gadget, on the side with room
        const QRect avail = screen()->availableGeometry();
        // on Wayland the gadget does not know its place: the one KWin reported (and we saved) instead
        const QRect g = Win7KWin::wayland() ? QRect(store().value(id + QStringLiteral("/Pos")).toPoint(), frameGeometry().size()) : frameGeometry();
        QPoint at(g.left() - m_flyout->width(), g.top());
        if (at.x() < avail.left()) {
            at.setX(g.left() + bodySize().width());
        }
        at.setY(qBound(avail.top(), at.y(), avail.bottom() - m_flyout->height()));
        if (Win7KWin::wayland()) {
            m_flyout->show();
            Win7KWin::moveTo(m_flyout, at);
        } else {
            m_flyout->move(at);
            m_flyout->show();
        }
        m_flyout->raise();
        m_flyout->activateWindow();
        update();
    }

    static QString dateText(const QDateTime &d)
    {
        if (!d.isValid()) {
            return {};
        }
        const qint64 mins = d.secsTo(QDateTime::currentDateTime()) / 60;
        if (mins < 1) {
            return tr7("just now");
        }
        if (mins < 60) {
            return tr7("%1 min ago").arg(mins);
        }
        if (mins < 24 * 60) {
            return tr7("%1 hours ago").arg(mins / 60);
        }
        return d.toLocalTime().toString(QStringLiteral("dd.MM.yyyy"));
    }

    QStringList m_feeds;
    int m_count = 20;
    QList<FeedItem> m_items;
    QList<FeedItem> m_incoming;
    int m_pending = 0;
    bool m_loading = false;
    int m_first = 0;
    QSet<QString> m_read;
    QString m_selected;
    QPointer<FeedFlyout> m_flyout;
    QTimer m_rotate;
    QTimer m_poll;
};
