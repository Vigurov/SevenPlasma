/*
 * music library (Audio.BrowseGallery / ArtistBrowsePage...): albums with their covers, artists, genres, songs,
 * playlists, composers, years and album artists of the Music folder.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: lib
    readonly property var pivots: ["albums", "artists", "genres", "songs", "playlists", "composers", "years", "album artists"]
    property int pivot: win.page.pivot || 0
    readonly property var filter: win.page.filter || null   // {field, value}

    function matches(song) { return !filter || String(song[filter.field] || "") === filter.value }
    readonly property var songs: library.songs.filter(s => matches(s))
    readonly property var albums: library.albums.filter(a => !filter || a.tracks.some(s => matches(s)))
    function groups(field) {
        const m = {}
        for (const s of songs) {
            const v = String(s[field] || "")
            if (v === "") continue
            if (!m[v]) m[v] = { big: v, text: v, value: v, image: s.cover || "" }
        }
        return Object.keys(m).sort((a, b) => a.localeCompare(b)).map(k => m[k])
    }
    readonly property var items: {
        switch (pivot) {
        case 0: return albums.map(a => ({ image: a.cover, text: a.album, album: a }))
        case 1: return groups("artist")
        case 2: return groups("genre")
        case 5: return groups("composer")
        case 6: return groups("year")
        case 7: return groups("albumArtist")
        default: return []
        }
    }
    readonly property var fieldOf: ({ 1: "artist", 2: "genre", 5: "composer", 6: "year", 7: "albumArtist" })

    function activate(i) {
        if (pivot === 0) { win.open({ kind: "album", album: items[i].album }); return }
        win.open({ kind: "music", pivot: 0, filter: { field: fieldOf[pivot], value: items[i].value } })
    }
    function setPivot(p) { pivot = (p + pivots.length) % pivots.length; win.page.pivot = pivot }
    Keys.onPressed: e => {
        if (e.key === Qt.Key_PageUp) { setPivot(pivot - 1); e.accepted = true }
        if (e.key === Qt.Key_PageDown) { setPivot(pivot + 1); e.accepted = true }
    }

    LibraryHeader {
        title: lib.filter ? lib.filter.value : win.tr("music library")
        commands: [{ t: win.tr("play all"), icon: "BROWSE.CMD.PLAY" }, { t: win.tr("search"), icon: "BROWSE.CMD.SEARCH" }]
        pivots: lib.pivots.map(p => win.tr(p))
        pivot: lib.pivot
        onPivotChosen: i => lib.setPivot(i)
        onCommandChosen: i => {
            if (i === 0 && lib.songs.length) { win.playSongs(lib.songs, 0); win.open({ kind: "nowplaying" }) }
            if (i === 1) lib.setPivot(3)
        }
    }

    Gallery {
        id: gallery
        visible: lib.pivot !== 3 && lib.pivot !== 4
        focus: visible
        x: 225; y: 246
        width: lib.width - 225
        height: 460
        items: lib.items
        rows: lib.pivot === 0 ? 2 : 3
        tileW: lib.pivot === 0 ? 152 : 230
        tileH: lib.pivot === 0 ? 152 : 92
        labelLines: lib.pivot === 0 ? 2 : 0
        noImage: lib.pivot === 0 ? "AUDIO.NOCOVER.1.LARGE" : "BROWSE.AUDIO.NOIMAGE"
        onActivated: i => lib.activate(i)
        Keys.onPressed: e => {
            if (e.key === Qt.Key_PageUp) { lib.setPivot(lib.pivot - 1); e.accepted = true }
            if (e.key === Qt.Key_PageDown) { lib.setPivot(lib.pivot + 1); e.accepted = true }
        }
    }

    SongList {
        visible: lib.pivot === 3
        focus: visible
        x: 225; y: 220
        width: lib.width - 225 - 120
        height: 470
        songs: lib.songs
        onActivated: i => { win.playSongs(lib.songs, i); win.open({ kind: "nowplaying" }) }
    }

    Text {
        visible: (lib.pivot === 4) || (lib.items.length === 0 && lib.pivot !== 3) || (lib.pivot === 3 && lib.songs.length === 0)
        x: 225; y: 260
        width: 800
        wrapMode: Text.WordWrap
        text: library.scanning ? win.tr("Windows Media Center is looking for music...")
              : lib.pivot === 4 ? win.tr("There are no playlists in your music library.")
              : win.tr("There is no music in your music library. Add music to the Music folder.")
        font.family: win.fontRegular
        font.pixelSize: 24
        color: win.lightBlue
    }
}
