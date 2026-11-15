/*
 * The animated background of Media Center's pages (PageBackground.mcml, AnimatedBackground): the blue picture and
 * three light layers at the bottom right whose transparency loops every 18 s (Launch.Layer1-3).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

Item {
    id: bg
    clip: true
    Image {
        anchors.fill: parent
        source: win.res("COMMON.ANIMATED.BACKGROUND")
        smooth: true
    }
    Image {
        id: layer3
        source: win.res("COMMON.BACKGROUND.LAYER3")
        width: Math.max(1366, parent.width); height: 302
        anchors { right: parent.right; bottom: parent.bottom }
        smooth: true
        SequentialAnimation on opacity {
            loops: Animation.Infinite
            NumberAnimation { from: 0.8; to: 0.1; duration: 4567 }
            NumberAnimation { to: 0.2; duration: 2600 }
            NumberAnimation { to: 0.8; duration: 8933 }
            PauseAnimation { duration: 1900 }
        }
    }
    Image {
        source: win.res("COMMON.BACKGROUND.LAYER2")
        width: 640; height: 678
        anchors { right: parent.right; bottom: parent.bottom; rightMargin: 44; bottomMargin: 28 }
        smooth: true
        opacity: 0
        SequentialAnimation on opacity {
            loops: Animation.Infinite
            NumberAnimation { from: 0.0; to: 0.6; duration: 3667 }
            NumberAnimation { to: 0.0; duration: 3833 }
            NumberAnimation { to: 0.1; duration: 2100 }
            PauseAnimation { duration: 2800 }
            NumberAnimation { to: 0.0; duration: 3833 }
            PauseAnimation { duration: 1767 }
        }
    }
    Image {
        source: win.res("COMMON.BACKGROUND.LAYER1")
        width: 445; height: 717
        anchors { right: parent.right; bottom: parent.bottom; bottomMargin: 3 }
        smooth: true
        opacity: 0
        SequentialAnimation on opacity {
            loops: Animation.Infinite
            PauseAnimation { duration: 3733 }
            NumberAnimation { from: 0.0; to: 0.6; duration: 1234 }
            PauseAnimation { duration: 866 }
            NumberAnimation { to: 0.0; duration: 3367 }
            NumberAnimation { to: 0.5; duration: 2467 }
            NumberAnimation { to: 0.4; duration: 2533 }
            NumberAnimation { to: 0.0; duration: 3800 }
        }
    }
}
