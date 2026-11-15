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
    // delegate data: image (url), text, sub; empty image shows the noImage picture
    property string noImage: "BROWSE.NOIMAGE"
    signal activated(int index)
    readonly property int cellH: tileH + (labelLines > 0 ? 16 + labelLines * 22 : 8)

    Keys.onRightPressed: currentIndex = Math.min(items.length - 1, currentIndex + rows)
    Keys.onLeftPressed: currentIndex = Math.max(0, currentIndex - rows)
    Keys.onDownPressed: if (currentIndex % rows < rows - 1 && currentIndex + 1 < items.length) currentIndex++
    Keys.onUpPressed: if (currentIndex % rows > 0) currentIndex--
    Keys.onReturnPressed: if (items.length) activated(currentIndex)
    Keys.onEnterPressed: if (items.length) activated(currentIndex)
    WheelHandler { onWheel: e => g.currentIndex = Math.max(0, Math.min(g.items.length - 1, g.currentIndex + (e.angleDelta.y > 0 ? -g.rows : g.rows))) }

    // keep the focused column in view, like the gallery's auto scroll
    property real scrollX: 0
    readonly property real colX: Math.floor(currentIndex / rows) * tileW
    onColXChanged: {
        if (colX - scrollX > width - tileW * 2) scrollX = colX - width + tileW * 2
        if (colX - scrollX < tileW) scrollX = Math.max(0, colX - tileW)
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
            x: Math.floor(index / g.rows) * g.tileW - g.scrollX
            y: (index % g.rows) * g.cellH
            width: g.tileW
            height: g.cellH
            visible: x > -g.tileW * 2 && x < g.width + g.tileW
            z: focused ? 2 : 1
            Item {
                id: pic
                width: g.tileW; height: g.tileH
                scale: t.focused ? 1.12 : 1
                Behavior on scale { NumberAnimation { duration: 150 } }
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
                    color: t.focused ? "#3c7ab8" : "#1e4374"
                    Image { x: 8; y: 4; width: 126; height: 97; source: t.modelData.collage ? (t.modelData.collage[0] || "") : ""; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 260 }
                    Image { x: 139; y: 4; width: 56; height: 47; source: t.modelData.collage ? (t.modelData.collage[1] || "") : ""; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 120 }
                    Image { x: 139; y: 54; width: 56; height: 47; source: t.modelData.collage ? (t.modelData.collage[2] || "") : ""; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 120 }
                    Text {
                        x: 16; y: 112; width: parent.width - 24
                        elide: Text.ElideRight
                        text: t.modelData.text || ""
                        font.family: win.fontRegular; font.pixelSize: 20
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
            Text {
                visible: g.labelLines > 0
                y: g.tileH + (t.focused ? 14 : 4)
                width: g.tileW - 6
                text: t.modelData.text || ""
                wrapMode: Text.WordWrap
                maximumLineCount: g.labelLines
                elide: Text.ElideRight
                font.family: win.fontRegular
                font.pixelSize: 18
                color: win.offWhite
                opacity: t.focused ? 1 : 0.85
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
