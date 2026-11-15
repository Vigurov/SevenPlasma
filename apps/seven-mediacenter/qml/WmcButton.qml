/*
 * A Media Center button (Common.Button.*: the dark frame, the blue glass when focused).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

Item {
    id: b
    property string text
    property bool focused: false
    property int fontSize: 22
    signal clicked()
    width: Math.max(200, label.implicitWidth + 60)
    height: 44
    readonly property bool lit: focused || ma.containsMouse
    onLitChanged: if (lit) win.sound("focus")
    BorderImage {
        anchors.fill: parent
        source: win.res(b.lit ? "COMMON.BUTTON.LEFT.FOCUS" : "COMMON.BUTTON.FIRSTRUN.NOFOCUS.FULL")
        border { left: 8; top: 8; right: 8; bottom: 8 }
    }
    Text {
        id: label
        anchors.centerIn: parent
        text: b.text
        font.family: win.fontRegular
        font.pixelSize: b.fontSize
        color: win.offWhite
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; onClicked: b.clicked() }
}
