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
    Keys.onUpPressed: header.enterPivots()
    LibraryHeader {
        id: header

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
        // 1RowGalleryItem4x3: 203x152
        tileW: 203; tileH: 152
        labelLines: 0
        items: pf.items.map(p => ({ image: p.file }))
        onActivated: i => win.open({ kind: "picture", items: pf.items, index: i, slideshow: false })
    }
    // the focused picture: its name, the date it was taken and its rating (measured on Windows 7)
    Column {
        x: 225; y: 455
        spacing: -3
        Text { text: pf.items[g.currentIndex] ? pf.items[g.currentIndex].name : ""; font.family: win.fontSemibold; font.pixelSize: 26; color: win.offWhite }
        Text { text: pf.items[g.currentIndex] ? pf.items[g.currentIndex].date : ""; font.family: win.fontRegular; font.pixelSize: 24; color: win.lightBlue }
        // the rating from the picture's EXIF
        Row {
            topPadding: 3; spacing: 8
            readonly property int stars: pf.items[g.currentIndex] ? (pf.items[g.currentIndex].rating || 0) : 0
            Repeater { model: 5; delegate: Image { required property int index; source: win.res(index < parent.stars ? "COMMON.STAR" : "COMMON.STAREMPTY"); width: 22; height: 21 } }
        }
    }
    Pager { current: g.currentIndex + 1; total: pf.items.length; opacity: g.opacity }
}
