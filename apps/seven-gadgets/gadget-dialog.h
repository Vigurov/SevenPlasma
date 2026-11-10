#include "win7i18n.h"
/*
 * Shared parts of the gadgets: the options sheet. Included inside main.cpp's namespace.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/** The options sheet of a gadget: dark header with its name, the body, OK / Cancel (like ClockSettings) */
class GadgetDialog : public QDialog
{
public:
    QVBoxLayout *body;

    explicit GadgetDialog(const QString &title, int width = 300)
    {
        setWindowTitle(title);
        setWindowIcon(QIcon::fromTheme(QStringLiteral("gadgets")));
        setFixedWidth(width);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 10);
        v->setSpacing(0);
        auto *header = new QLabel(title);
        header->setFixedHeight(44);
        header->setContentsMargins(16, 0, 0, 0);
        QFont hf = header->font();
        hf.setPixelSize(17);
        header->setFont(hf);
        header->setStyleSheet(QStringLiteral("QLabel { color: white; background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3a4350, stop:1 #12171e); }"));
        v->addWidget(header);

        auto *page = new QWidget;
        body = new QVBoxLayout(page);
        body->setContentsMargins(14, 12, 14, 8);
        body->setSpacing(4);
        v->addWidget(page, 1);

        auto *line = new QFrame;
        line->setFixedHeight(1);
        line->setStyleSheet(QStringLiteral("background: #dfdfdf;"));
        v->addWidget(line);
        v->addSpacing(10);
        auto *btns = new QHBoxLayout;
        btns->setContentsMargins(14, 0, 14, 0);
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        ok->setMinimumWidth(86);
        cancel->setMinimumWidth(86);
        ok->setDefault(true);
        btns->addWidget(ok);
        btns->addWidget(cancel);
        v->addLayout(btns);
        connect(ok, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }
};

/** "Calibri" of the gadgets; Carlito is its metric twin, otherwise the desktop font */
QFont gadgetFont(int px, bool bold = false)
{
    QFont f(QStringLiteral("Carlito"));
    if (!QFontInfo(f).exactMatch()) {
        f = QApplication::font();
    }
    f.setPixelSize(px);
    f.setBold(bold);
    return f;
}
