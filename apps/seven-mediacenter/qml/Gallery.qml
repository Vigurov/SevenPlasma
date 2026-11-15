/*
 * A Media Center gallery: tiles in rows that scroll sideways (BrowseGallery.mcml), the focused one larger with the
 * focus loop; arrows, Enter, the mouse and the wheel.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: g
    property var items: []
    property int rows: 2
    property int tileW: 152
    property int tileH: 152
    property int labelLines: 2
    property bool square: true
    property int currentIndex: 0
    onCurrentIndexChanged: win.sound("focus")
    // delegate data: image (url), text, sub; empty image shows the noImage picture
    property string noImage: "BROWSE.NOIMAGE"
    signal activated(int index)
    // 1-row items (BrowseGalleryItem.mcml: 1RowGalleryItem) are 218 high with their label; the gallery's grid has 2
    // between items (GalleryDefaultLayout Spacing="2, 2")
    readonly property int cellH: labelLines > 0 ? tileH + 66 : tileH + 2
    readonly property int cellW: tileW + 2
    // while the pivots or the commands have the focus, the gallery dims and nothing in it is focused
    opacity: activeFocus ? 1 : 0.75
    Behavior on opacity { NumberAnimation { duration: 300 } }

    // more items than fit: the gallery goes round (Repeat="WhenTooBig" RepeatGap="70"), the focused column stays
    // at the left and the ones before it show on the left
    readonly property int columns: Math.ceil(items.length / rows)
    readonly property bool wraps: columns * cellW > width
    readonly property real loopW: columns * cellW + 70
    function step(d) {
        const n = items.length
        if (!n) return
        let i = currentIndex + d
        if (wraps) i = ((i % n) + n) % n
        currentIndex = Math.max(0, Math.min(n - 1, i))
    }
    Keys.onRightPressed: step(rows)
    Keys.onLeftPressed: step(-rows)
    Keys.onDownPressed: if (currentIndex % rows < rows - 1 && currentIndex + 1 < items.length) currentIndex++
    // Up from the top row goes on to the page (its pivots)
    Keys.onUpPressed: e => { if (currentIndex % rows > 0) currentIndex--; else e.accepted = false }
    Keys.onReturnPressed: if (items.length) activated(currentIndex)
    Keys.onEnterPressed: if (items.length) activated(currentIndex)
    WheelHandler { onWheel: e => g.step(e.angleDelta.y > 0 ? -g.rows : g.rows) }

    // where column c shows: in a gallery that goes round, the copy nearest the left
    function shownX(col) {
        const x = col * cellW - scrollX
        if (!wraps) return x
        let m = ((x % loopW) + loopW) % loopW
        if (m > width) m -= loopW
        return m
    }
    // keep the focused column in view, like the gallery's auto scroll (locked at the left when it goes round)
    property real scrollX: 0
    readonly property real colX: Math.floor(currentIndex / rows) * cellW
    onColXChanged: {
        if (wraps) { scrollX += shownX(Math.floor(currentIndex / rows)); return }
        if (colX - scrollX > width - cellW * 2) scrollX = colX - width + cellW * 2
        if (colX - scrollX < cellW) scrollX = Math.max(0, colX - cellW)
    }
    Behavior on scrollX { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
    clip: false

    Repeater {
        model: g.items
        delegate: Item {
            id: t
            required property var modelData
            required property int index
            readonly property bool focused: index === g.currentIndex && g.activeFocus
            x: g.shownX(Math.floor(index / g.rows))
            y: (index % g.rows) * g.cellH
            width: g.cellW
            height: g.cellH
            visible: x > -g.tileW * 2 && x < g.width + g.tileW
            z: focused ? 2 : 1
            Item {
                id: pic
                width: g.tileW; height: g.tileH
                // the focused tile grows 26 % (FocusScale 1.26), mostly downwards (measured on Windows 7)
                transform: Scale {
                    origin.x: g.tileW / 2; origin.y: g.tileH * 0.18
                    xScale: t.focused ? 1.26 : 1; yScale: xScale
                    Behavior on xScale { NumberAnimation { duration: 150 } }
                }
                Rectangle { anchors.fill: parent; color: "#10305a"; visible: img.status !== Image.Ready && !t.modelData.collage }
                Image {
                    anchors.fill: parent
                    visible: img.status !== Image.Ready && !t.modelData.collage
                    source: win.res(g.noImage)
                    fillMode: Image.PreserveAspectFit
                }
                Image {
                    id: img
                    anchors.fill: parent
                    source: t.modelData.image || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: 300
                    smooth: true
                }
                Text {
                    visible: !t.modelData.image && t.modelData.big !== undefined
                    anchors.centerIn: parent
                    width: parent.width - 16
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: t.modelData.big || ""
                    font.family: win.fontRegular
                    font.pixelSize: 22
                    color: win.offWhite
                }
                // a folder of pictures: one big and two small pictures with the name, on the tile's own blue
                Rectangle {
                    visible: !!t.modelData.collage
                    anchors.fill: parent
                    color: t.focused ? "#4586ac" : "#1f4576"
                    Image { x: 8; y: 4; width: 126; height: 97; source: t.modelData.collage ? (t.modelData.collage[0] || "") : ""; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 260 }
                    Image { x: 139; y: 4; width: 56; height: 47; source: t.modelData.collage ? (t.modelData.collage[1] || "") : ""; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 120 }
                    Image { x: 139; y: 54; width: 56; height: 47; source: t.modelData.collage ? (t.modelData.collage[2] || "") : ""; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 120 }
                    Text {
                        x: 10; y: 112; width: parent.width - 18
                        elide: Text.ElideRight
                        text: t.modelData.text || ""
                        font.family: win.fontRegular; font.pixelSize: 21
                        color: win.offWhite
                    }
                }
                BorderImage {
                    visible: t.focused
                    anchors.fill: parent
                    anchors.margins: -4
                    source: win.res("SELECTOR")
                    border { left: 10; top: 10; right: 10; bottom: 10 }
                }
            }
            // the label under a 1-row item: 16 pt light blue, two lines in 140 (LabelTileSize); the focused item hides
            // it (its details are shown under the gallery)
            Text {
                visible: g.labelLines > 0 && !t.focused
                y: g.tileH - 1
                width: Math.max(140, g.tileW - 12)
                text: t.modelData.text || ""
                wrapMode: Text.WordWrap
                maximumLineCount: g.labelLines
                elide: Text.ElideRight
                lineHeight: 0.85
                font.family: win.fontRegular
                font.pixelSize: 21
                color: win.lightBlue
            }
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onEntered: { g.currentIndex = t.index; g.forceActiveFocus() }
                onClicked: g.activated(t.index)
            }
        }
    }
}
