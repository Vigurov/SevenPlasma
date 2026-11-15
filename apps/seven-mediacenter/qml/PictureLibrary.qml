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

    LibraryHeader {
        title: win.tr("picture library")
        commands: [{ t: win.tr("play slide show"), icon: "BROWSE.CMD.PLAY" }]
        pivots: lib.pivots.map(p => win.tr(p))
        pivot: 0
        onCommandChosen: { const a = lib.allPictures(); if (a.length) win.open({ kind: "picture", items: a, index: 0, slideshow: true }) }
    }
    Gallery {
        id: g
        focus: true
        x: 225; y: 230; width: lib.width - 225; height: 470
        rows: 2
        tileW: 202; tileH: 151
        labelLines: 0
        items: lib.folders.map(f => ({ text: f.name, folder: f }))
        onActivated: i => win.open({ kind: "folder", folder: lib.folders[i] })
    }
    // the folder tiles: one big and two small pictures, the name over them
    Repeater {
        model: lib.folders
        delegate: Item {
            required property var modelData
            required property int index
            x: g.x + Math.floor(index / 2) * 202 - g.scrollX + 4
            y: g.y + (index % 2) * g.cellH + 4
            width: 194; height: 143
            visible: x > 0 && x < lib.width
            Image { x: 0; y: 0; width: 126; height: 104; source: modelData.items[0] ? modelData.items[0].file : ""; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 260 }
            Image { x: 130; y: 0; width: 64; height: 50; source: modelData.items[1] ? modelData.items[1].file : ""; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 140 }
            Image { x: 130; y: 54; width: 64; height: 50; source: modelData.items[2] ? modelData.items[2].file : ""; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 140 }
            Text { x: 8; y: 110; width: 180; elide: Text.ElideRight; text: modelData.name; font.family: win.fontRegular; font.pixelSize: 18; color: win.offWhite }
        }
    }
    Text {
        visible: lib.folders.length === 0
        x: 225; y: 260
        text: win.tr("There are no pictures in your picture library. Add pictures to the Pictures folder.")
        font.family: win.fontRegular; font.pixelSize: 24; color: win.lightBlue
    }
}
