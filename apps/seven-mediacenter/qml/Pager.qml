/*
 * The item counter at the bottom right of a library page, "1 | 3": the focused item's number white and semibold,
 * a thin light blue bar, the count light blue (measured on Windows 7).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

Row {
    property int current: 1
    property int total: 0
    x: 1211; y: 662
    spacing: 4
    visible: total > 0
    Text { text: parent.current; font.family: win.fontSemibold; font.pixelSize: 23; color: win.offWhite; anchors.verticalCenter: bar.verticalCenter }
    Rectangle { id: bar; width: 1.4; height: 28; color: win.lightBlue }
    Text { text: parent.total; font.family: win.fontRegular; font.pixelSize: 23; color: win.lightBlue; anchors.verticalCenter: bar.verticalCenter }
}
