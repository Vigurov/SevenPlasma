/*
 * A picture full screen, or the slide show (cross-fading every 5 seconds); arrows go through the pictures.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: pv
    readonly property var items: win.page.items
    property int index: win.page.index
    property bool slideshow: win.page.slideshow
    property bool flip: false
    function show(i) {
        index = (i + items.length) % items.length
        flip = !flip
        ;(flip ? a : b).source = items[index].file
    }
    Component.onCompleted: { a.source = items[index].file; flip = true }
    Keys.onRightPressed: show(index + 1)
    Keys.onLeftPressed: show(index - 1)
    Keys.onSpacePressed: slideshow = !slideshow
    Timer { interval: 5000; running: pv.slideshow; repeat: true; onTriggered: pv.show(pv.index + 1) }

    Rectangle { anchors.fill: parent; color: "black" }
    Image { id: a; anchors.fill: parent; fillMode: Image.PreserveAspectFit; asynchronous: true; opacity: pv.flip ? 1 : 0; Behavior on opacity { NumberAnimation { duration: 800 } } }
    Image { id: b; anchors.fill: parent; fillMode: Image.PreserveAspectFit; asynchronous: true; opacity: pv.flip ? 0 : 1; Behavior on opacity { NumberAnimation { duration: 800 } } }
    MouseArea { anchors.fill: parent; onClicked: m => pv.show(pv.index + (m.x < width / 3 ? -1 : 1)) }

    // the song of the slide show, like "Sleep Away  Bob Acri" at the bottom left
    Item {
        visible: win.hasMedia && !win.videoMode && pv.slideshow
        x: 0; y: 640; width: 520; height: 96
        Image { anchors.fill: parent; source: win.res("NOWPLAYING.SLIDE.BOTTOM.BACKGROUND"); opacity: 0.9 }
        Image { x: 22; y: 16; width: 64; height: 64; source: win.current && win.current.cover ? win.current.cover : win.res("AUDIO.NOCOVER.1.SMALL"); fillMode: Image.PreserveAspectCrop }
        Column {
            x: 100; y: 14
            Text { text: win.current ? win.current.title : ""; font.family: win.fontSemibold; font.pixelSize: 22; color: win.offWhite }
            Text { text: win.current ? win.current.artist : ""; font.family: win.fontRegular; font.pixelSize: 18; color: win.offWhite }
        }
    }
}
