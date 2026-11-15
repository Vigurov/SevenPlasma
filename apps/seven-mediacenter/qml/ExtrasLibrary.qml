/*
 * extras library: the games of Windows 7 (as the Media Center of Windows 7 lists them among its extras).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: ex
    readonly property var games: library.games
    LibraryHeader { title: win.tr("extras library"); pivots: [win.tr("all"), win.tr("games")]; pivot: 0 }
    Gallery {
        focus: true
        x: 225; y: 230; width: ex.width - 225; height: 470
        rows: 2
        tileW: 160; tileH: 120
        labelLines: 1
        noImage: "COMMON.ICON.BLANK"
        items: ex.games.map(g => ({ image: library.iconUrl(g.icon, 96), text: g.name }))
        onActivated: i => library.launchDesktop(ex.games[i].desktop)
    }
    Text {
        visible: ex.games.length === 0
        x: 225; y: 260
        text: win.tr("There are no extras on this computer.")
        font.family: win.fontRegular; font.pixelSize: 24; color: win.lightBlue
    }
}
