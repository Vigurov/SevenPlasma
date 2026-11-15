/*
 * A folder of pictures: one row of thumbnails, the focused picture's name, date and rating under it
 * ("by name / by date / by rating").
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: pf
    readonly property var folder: win.page.folder
    property int pivot: 0
    readonly property var items: {
        const a = folder.items.slice()
        if (pivot === 1) a.sort((x, y) => x.time - y.time)
        return a
    }
    LibraryHeader {
        title: pf.folder.name
        commands: [{ t: win.tr("play slide show"), icon: "BROWSE.CMD.PLAY" }]
        pivots: [win.tr("by name"), win.tr("by date"), win.tr("by rating")]
        pivot: pf.pivot
        onPivotChosen: i => pf.pivot = i
        onCommandChosen: win.open({ kind: "picture", items: pf.items, index: 0, slideshow: true })
    }
    Gallery {
        id: g
        focus: true
        x: 225; y: 242; width: pf.width - 225; height: 200
        rows: 1
        tileW: 208; tileH: 152
        labelLines: 0
        items: pf.items.map(p => ({ image: p.file }))
        onActivated: i => win.open({ kind: "picture", items: pf.items, index: i, slideshow: false })
    }
    Column {
        x: 225; y: 438
        Text { text: pf.items[g.currentIndex] ? pf.items[g.currentIndex].name : ""; font.family: win.fontSemibold; font.pixelSize: 24; color: win.offWhite }
        Text { text: pf.items[g.currentIndex] ? pf.items[g.currentIndex].date : ""; font.family: win.fontRegular; font.pixelSize: 20; color: win.lightBlue }
        Row { topPadding: 4; spacing: 2; Repeater { model: 5; delegate: Image { source: win.res("COMMON.STAREMPTY"); width: 20; height: 20 } } }
    }
    Text {
        anchors { right: parent.right; rightMargin: 110; bottom: parent.bottom; bottomMargin: 74 }
        text: (g.currentIndex + 1) + " | " + pf.items.length
        font.family: win.fontRegular; font.pixelSize: 22; color: win.offWhite
    }
}
