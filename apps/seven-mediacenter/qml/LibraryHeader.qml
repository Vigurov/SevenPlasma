/*
 * The top of a library page: the big title at the right, the commands (play all, search...) and the pivots
 * ("< albums >  artists  genres ..."), positions measured on Windows 7 (1366x768 canvas).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

Item {
    id: h
    property string title
    property var commands: []   // [{t, icon}]
    property var pivots: []
    property int pivot: 0
    signal pivotChosen(int i)
    signal commandChosen(int i)
    anchors.fill: parent

    Text {
        anchors { right: parent.right; rightMargin: 56 }
        y: 36
        text: h.title
        font.family: win.fontLight
        font.pixelSize: 64
        color: win.lightBlue
        opacity: 0.45
    }
    Column {
        x: 184; y: 131 - h.commands.length * 34
        Repeater {
            model: h.commands
            delegate: Row {
                id: cmd
                required property var modelData
                required property int index
                spacing: 10
                height: 34
                Image { source: win.res(cmd.modelData.icon + (cmdMouse.containsMouse ? ".FOCUS" : "")); anchors.verticalCenter: parent.verticalCenter }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: cmd.modelData.t
                    font.family: win.fontRegular
                    font.pixelSize: 25
                    color: cmdMouse.containsMouse ? win.offWhite : win.cyan
                    MouseArea { id: cmdMouse; anchors.fill: parent; hoverEnabled: true; onClicked: h.commandChosen(cmd.index) }
                }
            }
        }
    }
    Row {
        x: 200; y: 136
        spacing: 63
        Repeater {
            model: h.pivots
            delegate: Row {
                id: pv
                required property string modelData
                required property int index
                readonly property bool sel: index === h.pivot
                spacing: 11
                // the next item follows the chosen one's right arrow closer than the usual gap
                width: implicitWidth - (sel ? 21 : 0)
                Image { visible: pv.sel; source: win.res("PIVOT.ARROW.LEFT.DORMANT"); anchors.verticalCenter: parent.verticalCenter }
                Text {
                    text: pv.modelData
                    font.family: pv.sel ? win.fontSemibold : win.fontRegular
                    font.pixelSize: pv.sel ? 30 : 24
                    color: pv.sel ? win.lightBlue : win.cyan
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea { anchors.fill: parent; onClicked: h.pivotChosen(pv.index) }
                }
                Image { visible: pv.sel; source: win.res("PIVOT.ARROW.RIGHT.DORMANT"); anchors.verticalCenter: parent.verticalCenter }
            }
        }
    }
}
