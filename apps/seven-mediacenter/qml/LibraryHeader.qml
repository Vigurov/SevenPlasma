/*
 * The top of a library page: the big title at the right, the commands (play all, search...) and the pivots
 * ("< albums >  artists  genres ..."), positions measured on Windows 7 (1366x768 canvas).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick
import QtQuick.Window

Item {
    id: h
    property string title
    property var commands: []   // [{t, icon}]
    property var pivots: []
    property int pivot: 0
    signal pivotChosen(int i)
    signal commandChosen(int i)
    anchors.fill: parent

    // the pivots have the focus (Up from the top of the page, or the mouse over them): the chosen one turns white,
    // the others light blue and the title fades, as in Windows 7
    readonly property bool pivotFocus: activeFocus || pivotHover.hovered
    property Item back: null
    function enter() { back = Window.activeFocusItem; forceActiveFocus() }
    function step(d) { if (pivots.length) choose((pivot + d + pivots.length) % pivots.length) }
    function choose(i) { if (i !== pivot) win.sound("focus"); pivotChosen(i) }
    Keys.onLeftPressed: step(-1)
    Keys.onRightPressed: step(1)
    Keys.onDownPressed: if (back) back.forceActiveFocus()

    Text {
        anchors { right: parent.right; rightMargin: 56 }
        y: 36
        text: h.title
        font.family: win.fontLight
        font.pixelSize: 64
        color: win.cyan
        opacity: h.pivotFocus ? 0.22 : 0.62
    }
    Column {
        x: 184; y: 131 - h.commands.length * 34
        Repeater {
            model: h.commands
            // a command (BrowseCommand.mcml): focused by the mouse it turns white, its icon becomes the framed
            // button and the focus loop plays on it (the glow at the top left, the spark running along the top,
            // the blue light at the bottom; 3 s loop)
            delegate: Item {
                id: cmd
                required property var modelData
                required property int index
                readonly property bool lit: cmdMouse.containsMouse
                onLitChanged: if (lit) win.sound("focus")
                width: row.width; height: 34
                Row {
                    id: row
                    spacing: 10
                    height: parent.height
                    Item {
                        id: icon
                        width: 29; height: 24
                        anchors.verticalCenter: parent.verticalCenter
                        Image {
                            visible: cmd.lit
                            x: -42; y: -22; width: icon.width + 67; height: icon.height + 34
                            source: win.res("BROWSE.CMD.FOCUSLOOP.TOPLEFT")
                            SequentialAnimation on opacity {
                                running: cmd.lit; loops: Animation.Infinite
                                NumberAnimation { from: 0; to: 0; duration: 267 }
                                NumberAnimation { to: 0.2; duration: 1000; easing.type: Easing.OutQuad }
                                NumberAnimation { to: 0; duration: 1300; easing.type: Easing.InQuad }
                                PauseAnimation { duration: 433 }
                            }
                        }
                        Image { source: win.res(cmd.modelData.icon + (cmd.lit ? ".FOCUS" : "")) }
                        Image {
                            visible: cmd.lit
                            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 2 }
                            source: win.res("BROWSE.CMD.FOCUSLOOP.BLUE")
                            SequentialAnimation on opacity {
                                running: cmd.lit; loops: Animation.Infinite
                                NumberAnimation { from: 0; to: 0.5; duration: 1133; easing.type: Easing.InOutSine }
                                NumberAnimation { to: 0; duration: 1434; easing.type: Easing.InOutSine }
                                PauseAnimation { duration: 433 }
                            }
                        }
                        Image {
                            visible: cmd.lit
                            y: 2
                            source: win.res("BROWSE.CMD.FOCUSLOOP.SMALL")
                            SequentialAnimation on x {
                                running: cmd.lit; loops: Animation.Infinite
                                NumberAnimation { from: -7; to: -7; duration: 167 }
                                NumberAnimation { to: 10; duration: 2400 }
                                PauseAnimation { duration: 433 }
                            }
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: cmd.modelData.t
                        font.family: win.fontRegular
                        font.pixelSize: 25
                        color: cmd.lit ? win.offWhite : win.cyan
                    }
                }
                MouseArea { id: cmdMouse; anchors.fill: parent; hoverEnabled: true; onClicked: h.commandChosen(cmd.index) }
            }
        }
    }
    Row {
        x: 200; y: 136
        spacing: 63
        HoverHandler { id: pivotHover }
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
                Image { visible: pv.sel; source: win.res("PIVOT.ARROW.LEFT" + (h.pivotFocus ? "" : ".DORMANT")); anchors.verticalCenter: parent.verticalCenter }
                Text {
                    text: pv.modelData
                    font.family: pv.sel ? win.fontSemibold : win.fontRegular
                    font.pixelSize: pv.sel ? 30 : 24
                    color: pv.sel ? (h.pivotFocus ? win.offWhite : win.lightBlue) : pvMouse.containsMouse ? win.offWhite : (h.pivotFocus ? win.lightBlue : win.cyan)
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea { id: pvMouse; anchors.fill: parent; hoverEnabled: true; onClicked: h.choose(pv.index) }
                }
                Image { visible: pv.sel; source: win.res("PIVOT.ARROW.RIGHT" + (h.pivotFocus ? "" : ".DORMANT")); anchors.verticalCenter: parent.verticalCenter }
            }
        }
    }
}
