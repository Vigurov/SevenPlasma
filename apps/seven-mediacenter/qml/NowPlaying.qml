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
    readonly property var menuItems: ["View Song List", "Visualize", "Play Pictures", "Shuffle", "Repeat"]
    property int cur: 0
    property bool songList: false
    property bool visualize: false
    function run(i) {
        if (i === 0) songList = !songList
        if (i === 1) visualize = !visualize
        if (i === 2) {
            let all = []
            for (const f of library.pictureFolders) all = all.concat(f.items)
            if (all.length) win.open({ kind: "picture", items: all, index: 0, slideshow: true })
        }
        if (i === 3) win.shuffle = !win.shuffle
        if (i === 4) win.repeat = !win.repeat
    }
    Keys.onUpPressed: cur = Math.max(0, cur - 1)
    Keys.onDownPressed: cur = Math.min(menuItems.length - 1, cur + 1)
    Keys.onReturnPressed: run(cur)
    Keys.onEnterPressed: run(cur)

    // the cover wall: every album of the library, tinted blue, slowly drifting
    Rectangle { anchors.fill: parent; color: "#0a2a5a" }
    Flow {
        id: wall
        width: parent.width + 248
        x: 0
        SequentialAnimation on x { loops: Animation.Infinite; NumberAnimation { from: 0; to: -124; duration: 30000 } NumberAnimation { from: -124; to: 0; duration: 30000 } }
        Repeater {
            model: {
                const covers = library.albums.map(a => a.cover).filter(c => c)
                const out = []
                const need = Math.ceil((np.width + 248) / 124) * 7
                for (let i = 0; covers.length && i < need; ++i) out.push(covers[(i * 7 + Math.floor(i / 11)) % covers.length])
                return out
            }
            delegate: Image { required property string modelData; width: 124; height: 124; source: modelData; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 160 }
        }
    }
    Rectangle { anchors.fill: parent; color: "#0b3f8c"; opacity: 0.62 }

    Column {
        x: 43; y: 216
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
                    font.family: mi.lit ? win.fontSemibold : win.fontRegular; font.pixelSize: 26
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
        x: 690; y: 278
        spacing: 6
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
        x: 363; y: 200; width: 880; height: 480
        songs: win.queue
        onActivated: i => win.playSongs(win.queue, i)
    }
}
