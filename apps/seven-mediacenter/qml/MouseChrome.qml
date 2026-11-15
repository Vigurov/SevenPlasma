/*
 * Media Center's mouse controls ("two foot" UI, drawn at their own pixel size, not scaled with the canvas):
 * Back and the Start button top left, the window buttons top right in full screen, the transport bar bottom
 * right. They come with the mouse and go after three quiet seconds.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick
import QtQuick.Window

Item {
    id: chrome
    property bool shown: false
    Timer { id: hideTimer; interval: 3000; onTriggered: chrome.shown = false }
    HoverHandler {
        id: hover
        onPointChanged: { chrome.shown = true; hideTimer.restart() }
    }

    component Button: Image {
        id: b
        property string name
        property bool enabledButton: true
        signal clicked()
        source: win.res("TWOFOOT.CONTROL." + name + (!enabledButton ? ".DISABLED" : ma.pressed ? ".PRESSED" : ma.containsMouse ? ".FOCUS" : ".NOFOCUS"))
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            enabled: b.enabledButton
            onClicked: b.clicked()
        }
    }
    component Divider: Image { source: win.res("TWOFOOT.CONTROL.DIVIDER"); anchors.verticalCenter: parent ? parent.verticalCenter : undefined }

    Item {
        anchors.fill: parent
        opacity: chrome.shown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        Image {
            visible: win.visibility === Window.FullScreen
            source: win.res("TWOFOOT.FULLSCREEN.TOPBAR.LEFT")
        }
        Row {
            x: 12; y: 10
            spacing: 6
            Button { name: "GOBACK"; enabledButton: win.stack.length > 1; onClicked: win.goBack() }
            Button { name: "MENU"; anchors.verticalCenter: parent.verticalCenter; onClicked: win.goHome() }
        }

        Image {
            visible: win.visibility === Window.FullScreen
            anchors.right: parent.right
            source: win.res("TWOFOOT.FULLSCREEN.TOPBAR.RIGHT")
            Row {
                anchors { right: parent.right; top: parent.top; rightMargin: 12; topMargin: 10 }
                Button { name: "MINIMIZE"; onClicked: win.showMinimized() }
                Button { name: "RESTORE"; onClicked: win.showNormal() }
                Button { name: "EXIT"; onClicked: Qt.quit() }
            }
        }

        Image {
            anchors { right: parent.right; bottom: parent.bottom }
            source: win.res("TWOFOOT.FULLSCREEN.BOTTOMBAR.RIGHT")
            Row {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter; verticalCenterOffset: 3 }
                spacing: 0
                // record, guide and the channel buttons need a TV tuner
                Button { name: "RECORD"; enabledButton: false }
                Button { name: "GUIDE"; enabledButton: false }
                Button { name: "MINUS"; enabledButton: false }
                Button { name: "PLUS"; enabledButton: false }
                Divider {}
                Button { name: "STOP"; enabledButton: win.hasMedia; onClicked: win.stop() }
                Divider {}
                Button { name: "BACKWARD"; enabledButton: win.hasMedia; onClicked: win.mediaPlayer.position = Math.max(0, win.mediaPlayer.position - 10000) }
                Button { name: "PREVIOUS"; enabledButton: win.hasMedia; onClicked: win.previous() }
                Button { name: win.playing ? "PAUSE" : "PLAY"; anchors.verticalCenter: parent.verticalCenter; enabledButton: win.hasMedia; onClicked: win.togglePlay() }
                Button { name: "NEXT"; enabledButton: win.hasMedia && !win.videoMode; onClicked: win.next() }
                Button { name: "FORWARD"; enabledButton: win.hasMedia; onClicked: win.mediaPlayer.position = win.mediaPlayer.position + 30000 }
                Divider {}
                Button { name: win.audioOutput.muted ? "MUTEON" : "MUTE"; onClicked: win.audioOutput.muted = !win.audioOutput.muted }
                Button { name: "MINUS"; onClicked: win.audioOutput.volume = Math.max(0, win.audioOutput.volume - 0.05) }
                Button { name: "PLUS"; onClicked: win.audioOutput.volume = Math.min(1, win.audioOutput.volume + 0.05) }
            }
        }
    }
}
