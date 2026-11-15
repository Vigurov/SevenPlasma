/*
 * A video playing full screen (the picture is the window's VideoOutput); the name and the time come with the mouse.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: vp
    Component.onCompleted: win.playVideo(win.page.video.file)
    Keys.onSpacePressed: win.togglePlay()
    Keys.onRightPressed: win.mediaPlayer.position = win.mediaPlayer.position + 30000
    Keys.onLeftPressed: win.mediaPlayer.position = Math.max(0, win.mediaPlayer.position - 10000)
    property bool info: true
    Timer { id: infoTimer; interval: 4000; running: true; onTriggered: vp.info = false }
    MouseArea { anchors.fill: parent; hoverEnabled: true; onPositionChanged: { vp.info = true; infoTimer.restart() } onDoubleClicked: win.visibility = win.visibility === 5 ? 2 : 5 }
    Item {
        visible: vp.info
        x: 0; y: 640; width: 640; height: 96
        Image { anchors.fill: parent; source: win.res("NOWPLAYING.SLIDE.BOTTOM.BACKGROUND"); opacity: 0.9 }
        Column {
            x: 30; y: 14
            Text { text: win.page.video.name; font.family: win.fontSemibold; font.pixelSize: 24; color: win.offWhite }
            Text { text: win.fmtTime(win.mediaPlayer.position / 1000) + " / " + win.fmtTime(win.mediaPlayer.duration / 1000); font.family: win.fontRegular; font.pixelSize: 20; color: win.lightBlue }
        }
    }
}
