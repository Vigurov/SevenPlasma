/*
 * Media Center's mouse controls ("two foot" UI): Back and the Start button top left, the window buttons top
 * right in full screen, the transport bar bottom right. They are part of the scaled 1366x768 canvas; the
 * positions are measured on Windows 7 (centres of the buttons). They come with the mouse and go after three
 * quiet seconds.
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
    // a button of the transport bar by its centre's distance from the right edge
    component BarButton: Button {
        property real fromRight
        x: chrome.width - fromRight - width / 2
        y: 738 - height / 2
    }
    component Divider: Image {
        property real fromRight
        source: win.res("TWOFOOT.CONTROL.DIVIDER")
        x: chrome.width - fromRight - width / 2
        y: 738 - height / 2
    }

    Item {
        anchors.fill: parent
        opacity: chrome.shown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        Image {
            visible: win.visibility === Window.FullScreen
            source: win.res("TWOFOOT.FULLSCREEN.TOPBAR.LEFT")
        }
        // Windows 7 keeps Back blue on the start menu too
        Button { x: 18; y: 13; name: "GOBACK"; onClicked: win.goBack() }
        Button { x: 62; y: 10; name: "MENU"; onClicked: win.goHome() }

        Image {
            visible: win.visibility === Window.FullScreen
            anchors.right: parent.right
            source: win.res("TWOFOOT.FULLSCREEN.TOPBAR.RIGHT")
        }
        Row {
            visible: win.visibility === Window.FullScreen
            anchors { right: parent.right; rightMargin: 20; top: parent.top; topMargin: 12 }
            spacing: 10
            Button { name: "MINIMIZE"; onClicked: win.showMinimized() }
            Button { name: "RESTORE"; onClicked: win.showNormal() }
            Button { name: "EXIT"; onClicked: Qt.quit() }
        }

        Image {
            anchors { right: parent.right; bottom: parent.bottom }
            source: win.res("TWOFOOT.FULLSCREEN.BOTTOMBAR.RIGHT")
            opacity: 0.6
        }
        // record, guide and the channel buttons need a TV tuner
        BarButton { fromRight: 548; name: "RECORD"; enabledButton: false }
        BarButton { fromRight: 507; name: "GUIDE"; enabledButton: false }
        BarButton { fromRight: 464; name: "MINUS"; enabledButton: false }
        BarButton { fromRight: 423; name: "PLUS"; enabledButton: false }
        Divider { fromRight: 399 }
        BarButton { fromRight: 377; name: "STOP"; enabledButton: win.hasMedia; onClicked: win.stop() }
        Divider { fromRight: 353 }
        BarButton { fromRight: 331; name: "BACKWARD"; enabledButton: win.hasMedia; onClicked: win.mediaPlayer.position = Math.max(0, win.mediaPlayer.position - 10000) }
        BarButton { fromRight: 289; name: "PREVIOUS"; enabledButton: win.hasMedia; onClicked: win.previous() }
        BarButton { fromRight: 245; name: win.playing ? "PAUSE" : "PLAY"; enabledButton: win.hasMedia; onClicked: win.togglePlay() }
        BarButton { fromRight: 201; name: "NEXT"; enabledButton: win.hasMedia && !win.videoMode; onClicked: win.next() }
        BarButton { fromRight: 159; name: "FORWARD"; enabledButton: win.hasMedia; onClicked: win.mediaPlayer.position = win.mediaPlayer.position + 30000 }
        Divider { fromRight: 133 }
        BarButton { fromRight: 113; name: win.audioOutput.muted ? "MUTEON" : "MUTE"; onClicked: win.audioOutput.muted = !win.audioOutput.muted }
        BarButton { fromRight: 70; name: "MINUS"; onClicked: win.audioOutput.volume = Math.max(0, win.audioOutput.volume - 0.05) }
        BarButton { fromRight: 29; name: "PLUS"; onClicked: win.audioOutput.volume = Math.min(1, win.audioOutput.volume + 0.05) }
    }
}
