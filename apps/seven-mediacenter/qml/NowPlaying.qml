/*
 * now playing of music (AudioNowPlaying.xml): the wall of album covers behind, the menu on the left
 * (View Song List, Visualize, Play Pictures, Shuffle, Repeat), the cover, the song, its time and rating.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: np
    readonly property var song: win.current
    readonly property var menuItems: ["View Song List", "Visualize", "Play Pictures", "Shuffle", "Repeat", "Buy Music"]
    property int cur: 0
    onCurChanged: win.sound("focus")
    property bool songList: false
    property bool visualize: false
    function run(i) {
        if (i !== 2) win.sound("mini")
        if (i === 0) songList = !songList
        if (i === 1) visualize = !visualize
        if (i === 2) {
            let all = []
            for (const f of library.pictureFolders) all = all.concat(f.items)
            if (all.length) win.open({ kind: "picture", items: all, index: 0, slideshow: true })
        }
        if (i === 3) win.shuffle = !win.shuffle
        if (i === 4) win.repeat = !win.repeat
        if (i === 5) win.open({ kind: "message", title: "Buy Music", text: "This feature is not available on this computer." })
    }
    Keys.onUpPressed: cur = Math.max(0, cur - 1)
    Keys.onDownPressed: cur = Math.min(menuItems.length - 1, cur + 1)
    Keys.onReturnPressed: run(cur)
    Keys.onEnterPressed: run(cur)

    // the mosaic (AlbumArtBackground.xml): 124x124 tiles from x -100 and y 0, album covers among the coloured
    // squares of Audio.NoCover.1-6, the rows slowly moving up
    Image { anchors.fill: parent; source: win.res("COMMON.BACKGROUND") }
    Item {
        id: mosaic
        anchors.fill: parent
        clip: true
        readonly property var covers: library.albums.map(a => a.cover).filter(c => c)
        readonly property int cols: Math.ceil((np.width + 100) / 124) + 1
        readonly property int rows: Math.ceil(np.height / 124) + 1
        // a fixed pseudo-random pattern, like a shuffled wall
        function tile(r, c) {
            let h = (r * 73856093) ^ (c * 19349663) ^ 0x5bd1e995
            h = (h ^ (h >>> 13)) * 1274126177; h = h ^ (h >>> 16)
            const v = Math.abs(h) % 100
            if (covers.length && v < 12) return covers[Math.abs(h >> 7) % covers.length]
            return win.res("AUDIO.NOCOVER." + (1 + Math.abs(h >> 3) % 6) + ".LARGE")
        }
        property real shift: 0
        NumberAnimation on shift { from: 0; to: 124; duration: 62000; loops: Animation.Infinite }
        Repeater {
            model: mosaic.rows * mosaic.cols
            delegate: Image {
                required property int index
                readonly property int r: Math.floor(index / mosaic.cols)
                readonly property int c: index % mosaic.cols
                readonly property int step: Math.floor(mosaic.shift / 124)
                x: -100 + c * 124
                y: r * 124 - (mosaic.shift % 124)
                width: 124; height: 124
                source: mosaic.tile(r + step, c)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize.width: 124
            }
        }
    }

    // the blue over the middle of the mosaic (behind the menu, the cover and the song) and at its left edge,
    // measured on Windows 7
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 110 / 768; color: "transparent" }
            GradientStop { position: 186 / 768; color: Qt.rgba(10 / 255, 45 / 255, 110 / 255, 0.6) }
            GradientStop { position: 250 / 768; color: Qt.rgba(10 / 255, 45 / 255, 110 / 255, 0.8) }
            GradientStop { position: 560 / 768; color: Qt.rgba(10 / 255, 45 / 255, 110 / 255, 0.76) }
            GradientStop { position: 682 / 768; color: Qt.rgba(10 / 255, 45 / 255, 110 / 255, 0.1) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }
    Rectangle {
        width: 340; height: parent.height
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(10 / 255, 45 / 255, 110 / 255, 0.7) }
            GradientStop { position: 0.25; color: Qt.rgba(10 / 255, 45 / 255, 110 / 255, 0.6) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    Column {
        x: 43; y: 198
        spacing: 3
        Repeater {
            model: np.menuItems
            delegate: Item {
                id: mi
                required property string modelData
                required property int index
                width: 252; height: 52
                readonly property bool lit: index === np.cur
                BorderImage {
                    visible: mi.lit
                    anchors.fill: parent
                    source: win.res("COMMON.BUTTON.LEFT.FOCUS")
                    border { left: 12; top: 12; right: 12; bottom: 12 }
                }
                Text {
                    x: 16; anchors.verticalCenter: parent.verticalCenter
                    text: win.tr(mi.modelData) + ((mi.index === 3 && win.shuffle) || (mi.index === 4 && win.repeat) ? "  ✓" : "")
                    font.family: win.fontSemibold; font.pixelSize: 26
                    color: mi.lit ? win.offWhite : win.lightBlue
                }
                MouseArea { anchors.fill: parent; hoverEnabled: true; onEntered: np.cur = mi.index; onClicked: np.run(mi.index) }
            }
        }
    }

    Image {
        visible: !np.songList && !np.visualize
        x: 363; y: 220; width: 300; height: 300
        source: np.song && np.song.cover ? np.song.cover : win.res("AUDIO.NOCOVER.1.LARGE")
        fillMode: Image.PreserveAspectCrop
    }
    Column {
        visible: !np.songList
        x: 690; y: 276
        spacing: 3
        Text { text: np.song ? np.song.title : ""; font.family: win.fontSemibold; font.pixelSize: 28; color: win.offWhite; width: 600; elide: Text.ElideRight }
        Text { text: np.song ? np.song.artist : ""; font.family: win.fontRegular; font.pixelSize: 26; color: win.offWhite }
        Text { text: np.song ? np.song.album : ""; font.family: win.fontRegular; font.pixelSize: 26; color: win.offWhite }
        Text { text: win.fmtTime(win.mediaPlayer.position / 1000); font.family: win.fontRegular; font.pixelSize: 28; color: win.lightBlue }
        Row {
            spacing: 2
            Repeater { model: 5; delegate: Image { source: win.res("COMMON.STAREMPTY"); width: 20; height: 20 } }
        }
    }
    // Visualize: bars of the music (Bars and Waves)
    Item {
        visible: np.visualize
        x: 330; y: 160; width: 900; height: 440
        Row {
            anchors.bottom: parent.bottom
            spacing: 3
            Repeater {
                model: 60
                delegate: Rectangle {
                    required property int index
                    width: 12
                    color: "#00b000"
                    property real phase: index * 0.37
                    height: win.playing ? 40 + Math.abs(Math.sin(phase + vt.t * (1.3 + (index % 7) * 0.21))) * 360 * (0.5 + 0.5 * Math.abs(Math.cos(vt.t * 0.7 + index))) : 6
                    anchors.bottom: parent.bottom
                }
            }
        }
        Timer { id: vt; property real t: 0; interval: 40; running: np.visualize && win.playing; repeat: true; onTriggered: t += 0.08 }
    }
    SongList {
        visible: np.songList
        anchors.fill: parent
        songs: win.queue
        onActivated: i => win.playSongs(win.queue, i)
    }
}
