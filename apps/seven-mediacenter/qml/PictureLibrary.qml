/*
 * picture library: the folders of the Pictures folder (a big picture and two small ones on each tile, as in
 * Windows 7), "play slide show".
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: lib
    readonly property var pivots: ["folders", "tags", "date taken", "ratings", "slide shows", "shared"]
    readonly property var folders: library.pictureFolders
    function allPictures() { let a = []; for (const f of folders) a = a.concat(f.items); return a }

    Keys.onUpPressed: header.enterPivots()
    LibraryHeader {
        id: header

        title: win.tr("picture library")
        commands: [{ t: win.tr("play slide show"), icon: "BROWSE.CMD.PLAY" }]
        pivots: lib.pivots.map(p => win.tr(p))
        pivot: 0
        onCommandChosen: { const a = lib.allPictures(); if (a.length) win.open({ kind: "picture", items: a, index: 0, slideshow: true }) }
    }
    Gallery {
        id: g
        focus: true
        x: 224; y: 246; width: lib.width - 224; height: 420
        rows: 1
        // 1RowGalleryItem4x3: 203x152
        tileW: 203; tileH: 152
        labelLines: 0
        items: lib.folders.map(f => ({ text: f.name, collage: f.items.slice(0, 3).map(p => p.file) }))
        onActivated: i => win.open({ kind: "folder", folder: lib.folders[i] })
    }
    // the focused folder: its place, the date and the number of pictures (as Windows 7 shows "Public Pictures : Sample Pictures")
    Column {
        visible: lib.folders.length > 0
        x: 225; y: 452
        readonly property var f: lib.folders[g.currentIndex] || null
        Text { text: parent.f ? win.tr("Pictures") + " : " + parent.f.name : ""; font.family: win.fontSemibold; font.pixelSize: 26; color: win.offWhite }
        Text { text: parent.f ? parent.f.items[0].date + "   " + win.tr("%1 items").arg(parent.f.items.length) : ""; font.family: win.fontRegular; font.pixelSize: 24; color: win.lightBlue }
    }
    Pager { current: g.currentIndex + 1; total: lib.folders.length; opacity: g.opacity }
    Text {
        visible: lib.folders.length === 0
        x: 225; y: 260
        text: win.tr("There are no pictures in your picture library. Add pictures to the Pictures folder.")
        font.family: win.fontRegular; font.pixelSize: 24; color: win.lightBlue
    }
}
