/*
 * extras library: the games of Windows 7 that have a Media Center tile (<Game>MCE.png), as Windows 7 lists them -
 * three rows of 122 tiles without labels, the focused game's name and "Play a game of ..." under the gallery,
 * "manage extras", the pivots "name | date added | most used" (measured on Windows 7).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: ex
    property int pivot: 0
    readonly property var games: library.games.filter(g => g.tile).sort((a, b) => a.name.localeCompare(b.name))
    Keys.onUpPressed: header.enterPivots()
    LibraryHeader {
        id: header
        targets: [g]
        title: win.tr("extras library")
        commands: [{ t: win.tr("manage extras"), icon: "BROWSE.CMD.SETTINGS" }]
        pivots: [win.tr("name"), win.tr("date added"), win.tr("most used")]
        pivot: ex.pivot
        onPivotChosen: i => ex.pivot = i
        onCommandChosen: win.open({ kind: "message", title: "manage extras", text: "This feature is not available on this computer." })
    }
    Gallery {
        id: g
        focus: true
        x: 225; y: 204; width: ex.width - 225; height: 380
        rows: 3
        tileW: 122; tileH: 122
        labelLines: 0
        items: ex.games.map(x => ({ image: x.tile }))
        onActivated: i => library.launchDesktop(ex.games[i].desktop)
    }
    Column {
        readonly property var it: ex.games[g.currentIndex] || null
        visible: g.activeFocus && it !== null
        x: 225 + Math.max(-30, Math.min(248, Math.floor(g.currentIndex / g.rows) * g.cellW - g.scrollX))
        y: 592
        spacing: -3
        Text { text: parent.it ? parent.it.name : ""; font.family: win.fontSemibold; font.pixelSize: 26; color: win.offWhite }
        Text { text: parent.it ? win.tr("Play a game of %1").arg(parent.it.name) : ""; font.family: win.fontRegular; font.pixelSize: 24; color: win.lightBlue }
    }
    Pager { current: g.currentIndex + 1; total: ex.games.length; opacity: g.opacity }
    Text {
        visible: ex.games.length === 0
        x: 225; y: 260
        text: win.tr("There are no extras on this computer.")
        font.family: win.fontRegular; font.pixelSize: 24; color: win.lightBlue
    }
}
