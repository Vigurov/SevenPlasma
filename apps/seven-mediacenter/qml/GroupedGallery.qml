/*
 * A gallery in groups (BrowseGroupedGallery.mcml), like "album artists": each group's covers (3-row items, 122) in
 * columns, its name in capitals turned on its side before them (16 pt semibold, medium blue). Measured on
 * Windows 7: the covers from y 235, a group's covers 28 after its name, the next name 38 after the covers.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: gg
    property var groups: []        // [{ name, items: [{ image, ... }] }]
    property int current: 0
    signal activated(var item)
    onCurrentChanged: win.sound("focus")
    onGroupsChanged: current = 0
    readonly property int rows: 3
    readonly property int tile: 122
    readonly property real topY: 235

    // every item with its place
    readonly property var cells: {
        const out = []
        let x = 224
        gg.groups.forEach((g, gi) => {
            if (gi > 0) x += 38 + 28
            g.items.forEach((it, i) => out.push({ item: it, group: gi, x: x + Math.floor(i / gg.rows) * (gg.tile + 2), y: gg.topY + (i % gg.rows) * (gg.tile + 2) }))
            x += Math.ceil(g.items.length / gg.rows) * (gg.tile + 2) - 2
        })
        return out
    }
    readonly property var labels: {
        const out = []
        gg.groups.forEach((g, gi) => {
            const first = gg.cells.find(c => c.group === gi)
            if (first) out.push({ name: g.name, x: first.x - (gi === 0 ? 55 : 35) })
        })
        return out
    }
    // left / right: the nearest item in the next column that way
    function move(dx, dy) {
        const c = cells[current]
        if (!c) return false
        let best = -1, score = 1e9
        cells.forEach((o, i) => {
            if (i === current) return
            const ddx = o.x - c.x, ddy = o.y - c.y
            if (dx && Math.sign(ddx) !== dx) return
            if (dy && (Math.sign(ddy) !== dy || Math.abs(ddx) > 1)) return
            const s = Math.abs(ddx) * 4 + Math.abs(ddy)
            if (s < score) { score = s; best = i }
        })
        if (best >= 0) current = best
        return best >= 0
    }
    Keys.onRightPressed: move(1, 0)
    Keys.onLeftPressed: move(-1, 0)
    Keys.onDownPressed: move(0, 1)
    Keys.onUpPressed: e => { if (!move(0, -1)) e.accepted = false }
    Keys.onReturnPressed: if (cells[current]) activated(cells[current].item)
    Keys.onEnterPressed: if (cells[current]) activated(cells[current].item)
    // keep the focused item in view
    property real scrollX: 0
    readonly property real curX: cells[current] ? cells[current].x : 0
    onCurXChanged: {
        if (curX - scrollX > width - 2 * tile) scrollX = curX - width + 2 * tile
        if (curX - scrollX < 224) scrollX = Math.max(0, curX - 224)
    }
    Behavior on scrollX { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }

    Item {
        anchors.fill: parent
        opacity: gg.activeFocus ? 1 : 0.75
        Behavior on opacity { NumberAnimation { duration: 300 } }
        Repeater {
            model: gg.labels
            delegate: Text {
                required property var modelData
                x: modelData.x - gg.scrollX + height
                y: gg.topY
                rotation: 90
                transformOrigin: Item.TopLeft
                text: modelData.name.toUpperCase()
                font.family: win.fontSemibold
                font.pixelSize: 24
                color: win.cyan
            }
        }
        Repeater {
            model: gg.cells
            delegate: Item {
                id: cell
                required property var modelData
                required property int index
                readonly property bool focused: index === gg.current && gg.activeFocus
                x: modelData.x - gg.scrollX; y: modelData.y
                width: gg.tile; height: gg.tile
                z: focused ? 2 : 1
                transform: Scale { origin.x: gg.tile / 2; origin.y: gg.tile * 0.18; xScale: cell.focused ? 1.26 : 1; yScale: xScale
                    Behavior on xScale { NumberAnimation { duration: 150 } } }
                Image {
                    anchors.fill: parent
                    source: cell.modelData.item.image || win.res("AUDIO.NOCOVER.1.LARGE")
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: 160
                }
                BorderImage {
                    visible: cell.focused
                    anchors.fill: parent
                    anchors.margins: -4
                    source: win.res("SELECTOR")
                    border { left: 10; top: 10; right: 10; bottom: 10 }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: { gg.current = cell.index; gg.forceActiveFocus() }
                    onClicked: gg.activated(cell.modelData.item)
                }
            }
        }
    }
}
