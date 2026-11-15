/*
 * music library (Audio.BrowseGallery / ArtistBrowsePage...): albums with their covers, artists, genres, songs,
 * playlists, composers, years and album artists of the Music folder.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: lib
    readonly property var pivots: ["albums", "artists", "genres", "songs", "playlists", "composers", "years", "album artists", "shared"]
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
    // a new pivot keeps the focus where it is (on the pivots); otherwise its content takes it
    onPivotChanged: if (!header.activeFocus) Qt.callLater(() => (pivot === 3 ? songList : gallery).forceActiveFocus())
    Keys.onPressed: e => {
        if (e.key === Qt.Key_PageUp) { setPivot(pivot - 1); e.accepted = true }
        if (e.key === Qt.Key_PageDown) { setPivot(pivot + 1); e.accepted = true }
    }

    Keys.onUpPressed: header.enterPivots()
    LibraryHeader {
        id: header
        targets: [gallery, songList]

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

    // albums: few of them make one row of 152 covers with labels, more make 3 rows of 122 (Audio1Row/3Row/
    // 5RowGalleryItem); artists, genres... are 3 rows
    readonly property int galleryRows: pivot === 0 ? (items.length <= 7 ? 1 : items.length <= 120 ? 3 : 5) : 3
    Gallery {
        id: gallery
        visible: lib.pivot !== 3 && lib.pivot !== 4 && lib.pivot !== 8
        focus: true
        x: 225; y: 244
        width: lib.width - 225
        height: 460
        items: lib.items
        rows: lib.galleryRows
        tileW: lib.galleryRows === 1 ? 152 : lib.galleryRows === 3 ? 122 : 72
        tileH: tileW
        labelLines: lib.galleryRows === 1 ? 2 : 0
        noImage: lib.pivot === 0 ? "AUDIO.NOCOVER.1.LARGE" : "BROWSE.AUDIO.NOIMAGE"
        onActivated: i => lib.activate(i)
        Keys.onPressed: e => {
            if (e.key === Qt.Key_PageUp) { lib.setPivot(lib.pivot - 1); e.accepted = true }
            if (e.key === Qt.Key_PageDown) { lib.setPivot(lib.pivot + 1); e.accepted = true }
        }
    }
    // the focused album's details under the gallery, at the album's column (ItemMetaData): the title 22 pt
    // semibold white, the artist and "1 track   3:20   2004" 18 pt light blue
    Column {
        readonly property var it: lib.items[gallery.currentIndex] || null
        visible: gallery.visible && gallery.activeFocus && it !== null && lib.pivot === 0
        // at the focused item's left, kept between 30 left of the gallery and 248 into it (DetailsMetadataLayoutInput)
        x: 225 + Math.max(-30, Math.min(248, Math.floor(gallery.currentIndex / gallery.rows) * gallery.cellW - gallery.scrollX))
        y: 244 + gallery.rows * gallery.cellH - 13
        spacing: -2
        Text { text: parent.it ? parent.it.text : ""; font.family: win.fontSemibold; font.pixelSize: 27; color: win.offWhite }
        Text { text: parent.it && parent.it.album ? parent.it.album.artist : ""; font.family: win.fontRegular; font.pixelSize: 24; color: win.lightBlue; transform: Translate { y: -2 } }
        Text {
            readonly property var a: parent.it ? parent.it.album : null
            text: a ? [a.tracks.length === 1 ? win.tr("1 track") : win.tr("%1 tracks").arg(a.tracks.length), win.fmtTime(a.duration), a.year].filter(x => x).join("  \u200a") : ""
            font.family: win.fontRegular; font.pixelSize: 24; color: win.lightBlue
            transform: Translate { y: -2 }
        }
    }
    Pager { visible: gallery.visible && lib.items.length > 0; current: gallery.currentIndex + 1; total: lib.items.length; opacity: gallery.opacity }

    SongList {
        id: songList
        visible: lib.pivot === 3
        anchors.fill: parent
        songs: lib.songs
        onActivated: i => { win.playSongs(lib.songs, i); win.open({ kind: "nowplaying" }) }
    }

    Text {
        visible: (lib.pivot === 4 || lib.pivot === 8) || (lib.items.length === 0 && lib.pivot !== 3) || (lib.pivot === 3 && lib.songs.length === 0)
        x: 225; y: 260
        width: 800
        wrapMode: Text.WordWrap
        text: library.scanning ? win.tr("Windows Media Center is looking for music...")
              : lib.pivot === 4 ? win.tr("There are no playlists in your music library.")
              : lib.pivot === 8 ? win.tr("No shared media libraries were found.")
              : win.tr("There is no music in your music library. Add music to the Music folder.")
        font.family: win.fontRegular
        font.pixelSize: 24
        color: win.lightBlue
    }
}
