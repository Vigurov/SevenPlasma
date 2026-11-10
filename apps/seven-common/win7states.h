/*
 * Windows 7 hover / pressed / selected pictures for widgets that carry their own style sheet
 * (a style sheet would otherwise replace the theme's drawing with flat colours).
 * The pictures are the original ones from aero.msstyles, in ~/.local/share/seven-aero (see README there):
 *  - tree and list items: Explorer::TreeView / Explorer::ListView 602 hot, 601 selected,
 *    603 hot + selected, 604 selected in an inactive window
 *  - toolbar buttons: Toolbar TP_BUTTON 849 hot, pressed, checked, hot + checked
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDir>
#include <QString>

namespace Win7States
{
inline QString picture(const QString &name)
{
    return QDir::homePath() + QStringLiteral("/.local/share/seven-aero/") + name + QStringLiteral(".png");
}

inline QString image(const QString &name)
{
    return QStringLiteral("border-image: url(%1) 3 3 3 3 stretch; border-width: 3px;").arg(picture(name));
}

// Rows of a QTreeView / QTreeWidget / QListView (pass the class name used in the sheet)
inline QString itemViewSheet(const QString &view = QStringLiteral("QTreeView"), int rowHeight = 18)
{
    return QStringLiteral("%1 { show-decoration-selected: 0; outline: 0; }"
                          "%1::item { height: %2px; border: 3px solid transparent; padding: 0px 1px; color: black; }" // %2 = row height minus the 3 px picture borders
                          "%1::item:hover { %3 }"
                          "%1::item:selected:active { %4 color: black; }"
                          "%1::item:selected:active:hover { %5 }"
                          "%1::item:selected:!active { %6 color: black; }")
        .arg(view)
        .arg(rowHeight - 6)
        .arg(image(QStringLiteral("item-hot")), image(QStringLiteral("item-selected")), image(QStringLiteral("item-hotselected")),
             image(QStringLiteral("item-selected-inactive")));
}

// QToolButtons of an MMC / Explorer style toolbar
inline QString toolButtonSheet()
{
    return QStringLiteral("QToolButton { border: 3px solid transparent; border-radius: 0px; padding: 0px; margin: 0px; background: transparent; min-width: 16px; min-height: 16px; }"
                          "QToolButton:hover { %1 }"
                          "QToolButton:pressed { %2 }"
                          "QToolButton:checked { %3 }"
                          "QToolButton:checked:hover { %4 }")
        .arg(image(QStringLiteral("toolbutton-hot")), image(QStringLiteral("toolbutton-pressed")), image(QStringLiteral("toolbutton-checked")),
             image(QStringLiteral("toolbutton-hotchecked")));
}
}
