/*
 * video library / movie library: the videos of the Videos folder with a frame of each.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: vl
    readonly property bool movies: win.page.title === "movie library"
    LibraryHeader {
        title: win.tr(win.page.title)
        commands: vl.movies ? [] : [{ t: win.tr("play all"), icon: "BROWSE.CMD.PLAY" }]
        pivots: vl.movies ? [win.tr("title"), win.tr("year"), win.tr("genre")] : [win.tr("folders"), win.tr("date taken"), win.tr("shared")]
        pivot: 0
        onCommandChosen: if (library.videos.length) win.open({ kind: "video", video: library.videos[0] })
    }
    Gallery {
        focus: true
        x: 225; y: 230; width: vl.width - 225; height: 470
        rows: vl.movies ? 2 : 3
        tileW: vl.movies ? 152 : 208
        tileH: vl.movies ? 214 : 117
        labelLines: vl.movies ? 2 : 1
        noImage: vl.movies ? "MOVIE.NOCOVER.1.LARGE" : "BROWSE.VIDEOITEM"
        items: library.videos.map(v => ({ image: v.thumb, text: v.name }))
        onActivated: i => win.open({ kind: "video", video: library.videos[i] })
    }
    Text {
        visible: library.videos.length === 0
        x: 225; y: 260
        text: win.tr("There are no videos in your library. Add videos to the Videos folder.")
        font.family: win.fontRegular; font.pixelSize: 24; color: win.lightBlue
    }
}
