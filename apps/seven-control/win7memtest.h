/*
 * "Windows Memory Diagnostic" (mdsched.exe): asks whether to restart now or check on the next start.
 * The check itself is Memtest86+, started once from GRUB (grub-reboot memtest86+).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "win7chrome.h"

#include <QApplication>
#include <QDialog>
#include <QMessageBox>
#include <QProcess>

namespace Win7Memtest
{
/** big command link with the green arrow, like the Windows 7 task dialogs */
class Choice : public QWidget
{
public:
    Choice(const QString &title, const QString &desc, std::function<void()> action)
        : m_action(std::move(action))
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(8, 6, 10, 8);
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(QStringLiteral("go-next")).pixmap(16, 16));
        h->addWidget(ic, 0, Qt::AlignTop);
        auto *v = new QVBoxLayout;
        v->setSpacing(2);
        auto *t = new QLabel(title);
        QFont f = t->font();
        f.setPixelSize(14);
        t->setFont(f);
        t->setStyleSheet(QStringLiteral("color: #151c55;"));
        t->setWordWrap(true);
        v->addWidget(t);
        auto *d = new QLabel(desc);
        d->setWordWrap(true);
        d->setStyleSheet(QStringLiteral("color: #151c55;"));
        v->addWidget(d);
        h->addLayout(v, 1);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!underMouse()) {
            return;
        }
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QColor(0xc6, 0xc6, 0xc6));
        p.setBrush(QColor(0xf8, 0xf8, 0xf8));
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
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) && m_action) {
            m_action();
        }
    }

private:
    std::function<void()> m_action;
};

/** grub-reboot through pkexec; false if refused or cancelled */
inline bool scheduleCheck(QWidget *parent)
{
    const int code = QProcess::execute(QStringLiteral("pkexec"), {QStringLiteral("grub-reboot"), QStringLiteral("memtest86+")});
    if (code != 0) {
        if (code != 126 && code != 127) {
            QMessageBox::warning(parent, QStringLiteral("Засіб перевірки пам'яті Windows"), QStringLiteral("Не вдалося запланувати перевірку пам'яті."));
        }
        return false;
    }
    return true;
}

inline int run()
{
    auto *dlg = new QDialog;
    dlg->setWindowTitle(QStringLiteral("Засіб перевірки пам'яті Windows"));
    dlg->setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-at-memtest")));
    dlg->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dlg->setFixedWidth(500);
    auto *v = new QVBoxLayout(dlg);
    v->setContentsMargins(0, 0, 0, 10);
    auto *white = new QWidget;
    white->setObjectName(QStringLiteral("win7mdwhite"));
    white->setAttribute(Qt::WA_StyledBackground, true);
    white->setStyleSheet(QStringLiteral("#win7mdwhite { background: #ffffff; }"));
    auto *wv = new QVBoxLayout(white);
    wv->setContentsMargins(16, 14, 16, 14);
    wv->setSpacing(8);
    auto *title = new QLabel(QStringLiteral("Перевірка комп'ютера на наявність проблем з пам'яттю"));
    QFont tf = title->font();
    tf.setPixelSize(16);
    title->setFont(tf);
    title->setStyleSheet(QStringLiteral("color: #1e3287;"));
    title->setWordWrap(true);
    wv->addWidget(title);
    auto *text = new QLabel(QStringLiteral("Проблеми з пам'яттю можуть призвести до втрати даних або до того, що комп'ютер перестане відповідати."));
    text->setWordWrap(true);
    wv->addWidget(text);
    wv->addSpacing(6);
    wv->addWidget(new Choice(QStringLiteral("Перезавантажити зараз і перевірити наявність проблем (рекомендовано)"),
                             QStringLiteral("Перш ніж вибрати цей параметр, збережіть роботу й закрийте всі програми."), [dlg] {
                                 if (scheduleCheck(dlg)) {
                                     QProcess::startDetached(QStringLiteral("qdbus6"), {QStringLiteral("org.kde.Shutdown"), QStringLiteral("/Shutdown"),
                                                                                        QStringLiteral("org.kde.Shutdown.logoutAndReboot")});
                                     dlg->accept();
                                 }
                             }));
    wv->addWidget(new Choice(QStringLiteral("Виконати перевірку під час наступного запуску комп'ютера"), QString(), [dlg] {
        if (scheduleCheck(dlg)) {
            dlg->accept();
        }
    }));
    v->addWidget(white, 1);
    auto *row = new QHBoxLayout;
    row->setContentsMargins(10, 10, 14, 0);
    row->addStretch(1);
    auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
    cancel->setMinimumWidth(75);
    row->addWidget(cancel);
    v->addLayout(row);
    QObject::connect(cancel, &QPushButton::clicked, dlg, &QDialog::reject);
    dlg->exec();
    return 0;
}
} // namespace Win7Memtest
