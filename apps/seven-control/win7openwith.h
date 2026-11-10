#pragma once
/* Windows 7 "Open with" (OpenWith.exe) dialog: header with the file name, a white box with "Recommended Programs"
 * and a collapsible "Other Programs" group of program tiles (32 px icon, name, grey second line), the
 * "Always use the selected program..." check box, Browse..., the web link line, OK / Cancel.
 * Measured on the reference VM (~/VMs/sevenplasma/refs/openwith_dlg*.png: 582 x 480 with frame, client 566 x 442).
 * `seven-control --open-with FILE...`; kio-win7's KOpenWithDialog hands local files here. */
#include "win7programs.h"
#include <KApplicationTrader>
#include <KIO/ApplicationLauncherJob>
#include <KService>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>

namespace Win7OpenWith
{
class ProgramList : public QWidget
{
public:
    QList<KService::Ptr> recommended, other;
    int selected = 0;            // index into recommended then other
    bool otherOpen = false;
    std::function<void()> onChange, onActivate;

    ProgramList()
    {
        setMouseTracking(true);
        QFont f(QStringLiteral("Segoe UI"));
        f.setPointSize(9);
        setFont(f);
    }
    KService::Ptr current() const
    {
        if (selected < recommended.size()) return recommended.value(selected);
        return otherOpen ? other.value(selected - recommended.size()) : KService::Ptr();
    }
    void relayout()
    {
        int rows = (recommended.size() + 1) / 2;
        int h = 14 + rows * 43 + 30;
        if (otherOpen) h += ((other.size() + 1) / 2) * 43 + 6;
        setMinimumHeight(h);
        update();
    }

protected:
    static constexpr int kCol = 236, kRow = 43;
    QRect tileRect(int idx) const
    {
        int y;
        int i = idx;
        if (idx < recommended.size()) {
            y = 22;
        } else {
            i = idx - recommended.size();
            y = otherHeaderY() + 16;
        }
        return QRect(4 + (i % 2) * kCol, y + (i / 2) * kRow, 231, 41);
    }
    int otherHeaderY() const { return 22 + ((recommended.size() + 1) / 2) * kRow + 14; }
    QRect chevronRect() const { return QRect(width() - 22, otherHeaderY() - 8, 16, 16); }
    void header(QPainter &p, const QString &text, int y)
    {
        p.setPen(QColor(0x1e, 0x39, 0x8b));
        p.drawText(QRect(13, y - 9, 300, 18), Qt::AlignLeft | Qt::AlignVCenter, text);
        const int x0 = 13 + p.fontMetrics().horizontalAdvance(text) + 5;
        QLinearGradient g(x0, 0, width() - 10, 0);
        g.setColorAt(0, QColor(0xa6, 0xb7, 0xd9));
        g.setColorAt(1, QColor(0xe2, 0xe8, 0xf3));
        p.fillRect(QRect(x0, y, width() - 10 - x0 - (text == m_otherText ? 24 : 0), 1), g);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        p.setRenderHint(QPainter::Antialiasing);
        header(p, QStringLiteral("Рекомендовані програми"), 12);
        const int n = recommended.size() + (otherOpen ? other.size() : 0);
        for (int i = 0; i < n; ++i) {
            const KService::Ptr s = i < recommended.size() ? recommended[i] : other[i - recommended.size()];
            const QRect r = tileRect(i);
            if (i == selected || i == m_hover) {
                QPainterPath path;
                path.addRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
                QLinearGradient g(0, r.top(), 0, r.bottom());
                if (i == selected) {
                    g.setColorAt(0, QColor(0xdc, 0xeb, 0xfc)); g.setColorAt(1, QColor(0xc1, 0xdb, 0xfc));
                    p.setPen(QColor(0x7d, 0xa2, 0xce));
                } else {
                    g.setColorAt(0, QColor(0xfa, 0xfb, 0xfd)); g.setColorAt(1, QColor(0xe8, 0xf1, 0xfb));
                    p.setPen(QColor(0xb8, 0xd6, 0xfb));
                }
                p.setBrush(g);
                p.drawPath(path);
            }
            const QIcon ic = QIcon::fromTheme(s->icon(), QIcon::fromTheme(QStringLiteral("application-x-executable")));
            p.drawPixmap(r.left() + 2, r.top() + 4, ic.pixmap(32, 32));
            p.setPen(Qt::black);
            p.drawText(QRect(r.left() + 36, r.top() + 5, r.width() - 40, 16), Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(s->name(), Qt::ElideRight, r.width() - 40));
            p.setPen(QColor(0x6d, 0x6d, 0x6d));
            const QString second = s->genericName().isEmpty() ? s->comment() : s->genericName();
            p.drawText(QRect(r.left() + 36, r.top() + 20, r.width() - 40, 16), Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(second, Qt::ElideRight, r.width() - 40));
        }
        // "Other Programs" group header with the round chevron button
        header(p, m_otherText, otherHeaderY());
        const QRect c = chevronRect();
        if (m_chevHover) {
            p.setPen(QColor(0x7d, 0xa2, 0xce));
            QLinearGradient g(0, c.top(), 0, c.bottom());
            g.setColorAt(0, QColor(0xf2, 0xf8, 0xfe)); g.setColorAt(1, QColor(0xc9, 0xe0, 0xf7));
            p.setBrush(g);
            p.drawEllipse(QRectF(c).adjusted(0.5, 0.5, -0.5, -0.5));
        }
        QPen pen(QColor(0x1e, 0x39, 0x8b), 1.6);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const QPointF m = QRectF(c).center();
        if (otherOpen) p.drawPolyline(QPolygonF{{m.x() - 3.5, m.y() + 2}, {m.x(), m.y() - 1.5}, {m.x() + 3.5, m.y() + 2}});
        else p.drawPolyline(QPolygonF{{m.x() - 3.5, m.y() - 1.5}, {m.x(), m.y() + 2}, {m.x() + 3.5, m.y() - 1.5}});
    }
    int hit(const QPoint &pt) const
    {
        const int n = recommended.size() + (otherOpen ? other.size() : 0);
        for (int i = 0; i < n; ++i) if (tileRect(i).contains(pt)) return i;
        return -1;
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int h = hit(e->position().toPoint());
        const bool ch = chevronRect().adjusted(-4, -4, 4, 4).contains(e->position().toPoint())
                        || QRect(10, otherHeaderY() - 9, 130, 18).contains(e->position().toPoint());
        if (h != m_hover || ch != m_chevHover) { m_hover = h; m_chevHover = ch; update(); }
    }
    void leaveEvent(QEvent *) override { m_hover = -1; m_chevHover = false; update(); }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (m_chevHover) {
            otherOpen = !otherOpen;
            if (!otherOpen && selected >= recommended.size()) selected = 0;
            relayout();
            if (onChange) onChange();
            return;
        }
        const int h = hit(e->position().toPoint());
        if (h >= 0) { selected = h; update(); if (onChange) onChange(); }
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        if (hit(e->position().toPoint()) >= 0 && onActivate) onActivate();
    }

private:
    int m_hover = -1;
    bool m_chevHover = false;
    const QString m_otherText = QStringLiteral("Інші програми");
};

class Dialog : public QDialog
{
public:
    explicit Dialog(const QStringList &files)
        : m_files(files)
    {
        setWindowTitle(QStringLiteral("Відкрити за допомогою"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("document-open")));
        setAttribute(Qt::WA_DeleteOnClose);
        QFont f(QStringLiteral("Segoe UI"));
        f.setPointSize(9);
        setFont(f);
        setFixedSize(566, 442);

        const QString first = files.value(0);
        m_mime = QMimeDatabase().mimeTypeForFile(first).name();

        auto *icon = new QLabel(this);
        icon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-openwith"), QIcon::fromTheme(QStringLiteral("system-search"))).pixmap(32, 32));
        icon->setGeometry(12, 11, 32, 32);
        auto *l1 = new QLabel(QStringLiteral("Виберіть програму, яку слід використати для відкриття цього файлу:"), this);
        l1->setGeometry(55, 9, 500, 18);
        auto *l2 = new QLabel(QStringLiteral("Файл:"), this);
        l2->setGeometry(55, 35, 40, 18);
        auto *l3 = new QLabel(QFileInfo(first).fileName(), this);
        l3->setGeometry(91, 35, 460, 18);

        m_list = new ProgramList;
        fillPrograms();
        auto *scroll = new QScrollArea(this);
        scroll->setGeometry(11, 58, 543, 278);
        scroll->setWidget(m_list);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::Box);
        scroll->setStyleSheet(QStringLiteral("QScrollArea { border: 1px solid #828790; background: white; }"));
        m_list->relayout();

        m_always = new QCheckBox(QStringLiteral("Завжди використовувати вибрану програму для відкриття файлів цього типу"), this);
        m_always->setGeometry(13, 343, 460, 18);
        m_always->setChecked(true);
        auto *browse = new QPushButton(QStringLiteral("Огляд..."), this);
        browse->setGeometry(480, 345, 73, 23);

        auto *web = new QLabel(QStringLiteral("Якщо потрібної програми немає у списку або на комп'ютері, можна "
                                              "<a href=\"x\" style=\"color:#0066cc\">знайти відповідну програму в Інтернеті</a>."),
                               this);
        web->setWordWrap(true);
        web->setGeometry(13, 373, 540, 32);
        web->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        connect(web, &QLabel::linkActivated, this, [this] {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.google.com/search?q=") + QString::fromUtf8(QUrl::toPercentEncoding(m_mime + QStringLiteral(" linux app")))));
        });

        m_ok = new QPushButton(QStringLiteral("OK"), this);
        m_ok->setGeometry(399, 410, 73, 23);
        m_ok->setDefault(true);
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"), this);
        cancel->setGeometry(480, 410, 73, 23);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_ok, &QPushButton::clicked, this, [this] { run(m_list->current()); });
        m_list->onChange = [this] { m_ok->setEnabled(bool(m_list->current())); };
        m_list->onActivate = [this] { run(m_list->current()); };
        m_ok->setEnabled(bool(m_list->current()));

        connect(browse, &QPushButton::clicked, this, [this] {
            const QString exe = QFileDialog::getOpenFileName(this, QStringLiteral("Відкрити за допомогою..."), QStringLiteral("/usr/bin"));
            if (exe.isEmpty()) return;
            KService::Ptr s(new KService(QFileInfo(exe).fileName(), exe + QStringLiteral(" %F"), QString()));
            run(s, false);
        });
    }

private:
    void fillPrograms()
    {
        const auto rec = KApplicationTrader::queryByMimeType(m_mime);
        QSet<QString> seen;
        const KService::Ptr pref = KApplicationTrader::preferredService(m_mime);
        if (pref) { m_list->recommended << pref; seen << pref->storageId(); }
        for (const KService::Ptr &s : rec) {
            if (seen.contains(s->storageId())) continue;
            seen << s->storageId();
            m_list->recommended << s;
        }
        for (const KService::Ptr &s : Win7Programs::defaultablePrograms()) {
            if (seen.contains(s->storageId())) continue;
            if (s->exec().isEmpty() || s->terminal()) continue;
            seen << s->storageId();
            m_list->other << s;
        }
        if (m_list->recommended.isEmpty()) m_list->otherOpen = true;
    }
    void run(const KService::Ptr &s, bool canRemember = true)
    {
        if (!s) return;
        if (canRemember && m_always->isChecked() && !s->storageId().isEmpty()) {
            Win7Programs::setDefaults({m_mime}, s->storageId());
        }
        QList<QUrl> urls;
        for (const QString &f : m_files) urls << QUrl::fromLocalFile(f);
        auto *job = new KIO::ApplicationLauncherJob(s);
        job->setUrls(urls);
        hide();                                            // close only after the program has been started
        connect(job, &KJob::finished, this, [this] { accept(); });
        job->start();
    }

    QStringList m_files;
    QString m_mime;
    ProgramList *m_list = nullptr;
    QCheckBox *m_always = nullptr;
    QPushButton *m_ok = nullptr;
};
}
