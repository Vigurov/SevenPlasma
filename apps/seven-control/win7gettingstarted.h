#pragma once
/*
 * "Getting Started" of Windows 7 (Control Panel\All Control Panel Items\Getting Started, OobeFldr.dll).
 *
 * The tasks, their texts and icons follow the folder's XML (OobeFldr.dll XML 1): the selected task is described
 * in the "welcome" banner (OobeFldr.dll bitmap 102) with its big icon and the link that starts it; a task starts
 * with a double click, Enter or that link.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "win7i18n.h"

#include <QDesktopServices>
#include <QGridLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

namespace Win7GettingStarted
{
enum {
    GettingStartedView = 99,
};

struct Task {
    QString key;
    QString name;            // under the icon in the list
    QString title;           // the banner's heading
    QList<QPair<bool, QString>> paras; // (bullet, text)
    QString action;          // the banner's link
};

inline QList<Task> tasks()
{
    return {
        {QStringLiteral("discover"), tr7("Go online to find out what's new in Windows 7"), tr7("Go online to find out what's new in Windows 7"),
         {{true, tr7("Discover new features that can help you be productive, stay safer, and have fun")},
          {true, tr7("Find out everything you need to know about setting up your computer")}},
         tr7("Go online to learn more")},
        {QStringLiteral("personalize"), tr7("Personalize Windows"), tr7("Personalize Windows"),
         {{true, tr7("Make your computer look the way you want it to")},
          {true, tr7("Choose a theme to change the desktop background, window color, sounds, and screen saver all at once")},
          {true, tr7("Create and save your own themes")},
          {true, tr7("Share themes with friends and family")},
          {true, tr7("Or change the pictures, color, and sounds individually")}},
         tr7("Personalize Windows")},
        {QStringLiteral("transfer"), tr7("Transfer files and settings from another computer"), tr7("Transfer files and settings from another computer"),
         {{true, tr7("Make the move to your new computer easier by transferring files and settings from your old computer all at once")},
          {true, tr7("Move documents, music, photos, favorites, e-mail, program settings, and more")}},
         tr7("Open Windows Easy Transfer")},
        {QStringLiteral("homegroup"), tr7("Use a homegroup to share with other computers in your home"), tr7("Use a homegroup to share with other computers in your home"),
         {{true, tr7("Share pictures, music, videos, documents, and printers with other computers in your homegroup")},
          {true, tr7("Choose which files and folders to share")},
          {true, tr7("Create a new homegroup or add this computer to an existing one")}},
         tr7("Set up a homegroup")},
        {QStringLiteral("uac"), tr7("Choose when to be notified about changes to your computer"), tr7("Choose when to be notified about changes to your computer"),
         {{false, tr7("Make changes to your User Account Control settings")}},
         tr7("Change settings")},
        {QStringLiteral("live"), tr7("Go online to get Windows Live Essentials"), tr7("Go online to get Windows Live Essentials"),
         {{false, tr7("Windows Live Essentials is a set of free programs to help you:")},
          {true, tr7("Stay in touch with the people you care about using Windows Live Messenger and Mail")},
          {true, tr7("Edit and publish photos, blogs, and videos using Windows Live Photo Gallery, Writer, and Movie Maker")},
          {true, tr7("Customize your web browser and stay safer online using Windows Live Toolbar and Family Safety")}},
         tr7("Go online to get Windows Live Essentials")},
        {QStringLiteral("backup"), tr7("Back up your files"), tr7("Back up your files"),
         {{true, tr7("Automatically save copies of your photos, music, and other files")},
          {true, tr7("Set up automatic backup and learn about other ways to recover information")}},
         tr7("Back up your files")},
        {QStringLiteral("users"), tr7("Add new users to your computer"), tr7("Add new users to your computer"),
         {{true, tr7("Give other users access to this computer")},
          {true, tr7("Change your account type")},
          {true, tr7("Change your account picture")}},
         tr7("Add new users")},
        {QStringLiteral("textsize"), tr7("Change the size of the text on your screen"), tr7("Change the size of the text on your screen"),
         {{true, tr7("Make text and other items on your screen larger or smaller")},
          {true, tr7("Adjust your screen resolution")}},
         tr7("Change text size")},
    };
}

inline QPixmap picture(const QString &name)
{
    return QPixmap(QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-control/gettingstarted/") + name + QStringLiteral(".png")));
}

/** Windows 7's selection / hot box of shell items (the same colours as the navigation pane) */
inline void paintItemBox(QPainter &p, const QRectF &box, bool selected, bool hot, bool focus)
{
    if (!selected && !hot) {
        return;
    }
    QColor top, bottom, border;
    if (selected && (focus || hot)) {
        top = QColor(0xdc, 0xeb, 0xfc);
        bottom = QColor(0xc1, 0xdb, 0xfc);
        border = hot ? QColor(0x7d, 0xa2, 0xce) : QColor(0x84, 0xac, 0xdd);
    } else if (selected) {
        top = QColor(0xf8, 0xf8, 0xf8);
        bottom = QColor(0xe5, 0xe5, 0xe5);
        border = QColor(0xd9, 0xd9, 0xd9);
    } else {
        top = QColor(0xfa, 0xfb, 0xfd);
        bottom = QColor(0xe8, 0xf1, 0xfc);
        border = QColor(0xb8, 0xd6, 0xfb);
    }
    QLinearGradient g(box.topLeft(), box.bottomLeft());
    g.setColorAt(0, top);
    g.setColorAt(1, bottom);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(border);
    p.setBrush(g);
    p.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    p.restore();
}

class Page : public QWidget
{
public:
    explicit Page(std::function<void(const QString &)> run, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_tasks(tasks())
        , m_run(std::move(run))
    {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setAutoFillBackground(true);
        QPalette pal = palette();
        pal.setColor(QPalette::Window, QColor(0xfc, 0xfc, 0xfc));
        setPalette(pal);
        setMinimumHeight(kBanner + 6 + 3 * kRowPitch + 10);
    }

protected:
    static constexpr int kBanner = 196;
    static constexpr int kColPitch = 254;
    static constexpr int kRowPitch = 56;
    static constexpr QSize kItem{250, 51};

    QRect itemRect(int i) const
    {
        const int cols = qMax(1, (width() - 14) / kColPitch);
        return QRect(14 + (i % cols) * kColPitch, kBanner + 6 + (i / cols) * kRowPitch, kItem.width(), kItem.height());
    }
    QRect actionRect() const
    {
        const QFontMetrics fm(actionFont());
        const int w = 17 + 2 + fm.horizontalAdvance(m_tasks.at(m_sel).action);
        // the first task's link sits higher: the "welcome" word fills the bottom of the banner
        const int y = m_sel == 0 ? 51 : kBanner - 32;
        return QRect(width() - 12 - w, y, w, qMax(18, fm.height()));
    }
    static QFont titleFont()
    {
        QFont f;
        f.setPointSizeF(f.pointSizeF() * 15.0 / 9.0);
        return f;
    }
    static QFont actionFont()
    {
        QFont f;
        f.setPointSizeF(f.pointSizeF() * 11.0 / 9.0);
        return f;
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0xfc, 0xfc, 0xfc));
        const Task &t = m_tasks.at(m_sel);

        // banner
        const QRect banner(0, 0, width(), kBanner);
        const QPixmap bg = picture(QStringLiteral("banner"));
        if (!bg.isNull()) {
            p.drawPixmap(banner, bg);
        } else {
            QLinearGradient g(banner.topLeft(), banner.topRight());
            g.setColorAt(0, QColor(0x91, 0xbd, 0xf4));
            g.setColorAt(0.5, QColor(0xe8, 0xf1, 0xfd));
            g.setColorAt(1, QColor(0xbd, 0xd1, 0xfa));
            p.fillRect(banner, g);
        }
        if (m_sel == 0) {
            QFont wf(QStringLiteral("Segoe UI Light"));
            wf.setPixelSize(80);
            p.setFont(wf);
            p.setPen(QColor(0x5d, 0x75, 0x9b, 150));
            const QFontMetrics fm(wf);
            const QString w = tr7("welcome");
            p.drawText(width() - 4 - fm.horizontalAdvance(w), kBanner - 2, w);
        }
        p.drawPixmap(14, 30, QIcon::fromTheme(QStringLiteral("seven-gs-") + t.key).pixmap(128, 128));
        p.setPen(Qt::black);
        p.setFont(titleFont());
        const int tx = 161;
        p.drawText(QRect(tx, 17, width() - tx - 20, QFontMetrics(titleFont()).height()), Qt::AlignLeft | Qt::AlignTop, t.title);
        p.setFont(font());
        const QFontMetrics fm(font());
        int y = 17 + QFontMetrics(titleFont()).height() + 1;
        const int textW = qMin(width() - tx - 260, 640);
        for (const auto &para : t.paras) {
            const int x = para.first ? tx + 10 : tx + 1;
            if (para.first) {
                p.drawText(QRect(tx + 3, y, 8, fm.height()), Qt::AlignLeft | Qt::AlignTop, QStringLiteral("•"));
            }
            const QRect br = fm.boundingRect(QRect(x, y, textW, 400), Qt::TextWordWrap, para.second);
            p.drawText(QRect(x, y, textW, br.height()), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, para.second);
            y += br.height() - qMax(1, br.height() / fm.height()); // GDI lines are a pixel closer than Qt's

        }
        // the link
        const QRect ar = actionRect();
        p.drawPixmap(ar.left(), ar.top() + (ar.height() - 18) / 2, picture(m_actionHot ? QStringLiteral("arrow-hot") : QStringLiteral("arrow")));
        p.setFont(actionFont());
        p.setPen(m_actionHot ? QColor(0x07, 0x4a, 0xe5) : QColor(0x1e, 0x39, 0x5b));
        QFont af = actionFont();
        af.setUnderline(m_actionHot);
        p.setFont(af);
        p.drawText(ar.adjusted(19, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, t.action);

        // tasks
        p.setFont(font());
        for (int i = 0; i < m_tasks.size(); ++i) {
            const QRect r = itemRect(i);
            paintItemBox(p, r, i == m_sel, i == m_hot, hasFocus());
            p.drawPixmap(r.left() + 5, r.top() + (r.height() - 48) / 2, QIcon::fromTheme(QStringLiteral("seven-gs-") + m_tasks.at(i).key).pixmap(48, 48));
            const QRect text(r.left() + 57, r.top(), r.width() - 57 - 4, r.height());
            p.setPen(Qt::black);
            p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, m_tasks.at(i).name);
        }
    }

    int itemAt(const QPoint &pos) const
    {
        for (int i = 0; i < m_tasks.size(); ++i) {
            if (itemRect(i).contains(pos)) {
                return i;
            }
        }
        return -1;
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int hot = itemAt(e->pos());
        const bool actionHot = actionRect().contains(e->pos());
        if (hot != m_hot || actionHot != m_actionHot) {
            m_hot = hot;
            m_actionHot = actionHot;
            setCursor(actionHot ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hot = -1;
        m_actionHot = false;
        update();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) {
            return;
        }
        if (actionRect().contains(e->pos())) {
            m_run(m_tasks.at(m_sel).key);
            return;
        }
        const int i = itemAt(e->pos());
        if (i >= 0) {
            m_sel = i;
            update();
        }
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        const int i = itemAt(e->pos());
        if (i >= 0) {
            m_run(m_tasks.at(i).key);
        }
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        const int cols = qMax(1, (width() - 14) / kColPitch);
        int s = m_sel;
        switch (e->key()) {
        case Qt::Key_Left:
            s = qMax(0, s - 1);
            break;
        case Qt::Key_Right:
            s = qMin(int(m_tasks.size()) - 1, s + 1);
            break;
        case Qt::Key_Up:
            s = s - cols >= 0 ? s - cols : s;
            break;
        case Qt::Key_Down:
            s = s + cols < m_tasks.size() ? s + cols : s;
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            m_run(m_tasks.at(m_sel).key);
            return;
        default:
            QWidget::keyPressEvent(e);
            return;
        }
        m_sel = s;
        update();
    }
    void focusInEvent(QFocusEvent *) override
    {
        update();
    }
    void focusOutEvent(QFocusEvent *) override
    {
        update();
    }

private:
    QList<Task> m_tasks;
    std::function<void(const QString &)> m_run;
    int m_sel = 0;
    int m_hot = -1;
    bool m_actionHot = false;
};

inline QWidget *build(std::function<void(const QString &)> run)
{
    auto *page = new Page(std::move(run));
    QTimer::singleShot(0, page, [page]() {
        page->setFocus(); // the list has the keyboard, its selection is blue (as in Explorer)
    });
    return page;
}
}
