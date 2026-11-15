/*
 * The details of an album (Audio.Album.Details.xml): "album  actions", its name, artist, tracks / length / year,
 * the cover, "add to now playing", "play album" and the songs.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: d
    readonly property var album: win.page.album
    readonly property var rows: [{ cmd: "add" }, { cmd: "play" }].concat(album.tracks.map((t, i) => ({ track: t, i: i })))
    property int cur: 1
    onCurChanged: win.sound("focus")
    function run(r) {
        if (r.cmd === "add") { win.addToQueue(album.tracks); return }
        if (r.cmd === "play") { win.playSongs(album.tracks, 0); win.open({ kind: "nowplaying" }); return }
        win.playSongs(album.tracks, r.i)
        win.open({ kind: "nowplaying" })
    }
    Keys.onUpPressed: cur = Math.max(0, cur - 1)
    Keys.onDownPressed: cur = Math.min(rows.length - 1, cur + 1)
    Keys.onReturnPressed: run(rows[cur])
    Keys.onEnterPressed: run(rows[cur])

    Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.35 }
    Rectangle {
        id: panel
        x: 280; width: 805; height: parent.height
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0d3a74" }
            GradientStop { position: 0.34; color: "#1a5596" }
            GradientStop { position: 0.35; color: "#3d7cc0" }
            GradientStop { position: 1.0; color: "#2865a8" }
        }
        Text { id: albumWord; x: 84; y: 19; text: win.tr("album"); font.family: win.fontLight; font.pixelSize: 65; color: win.offWhite }
        Text { x: albumWord.x + albumWord.implicitWidth + 26; y: 41; text: win.tr("actions"); font.family: win.fontLight; font.pixelSize: 36; color: win.lightBlue; opacity: 0.6 }
        Column {
            x: 84; y: 118; spacing: -3
            Text { text: d.album.album; font.family: win.fontSemibold; font.pixelSize: 26; color: win.offWhite; width: 560; elide: Text.ElideRight }
            Text { text: d.album.artist; font.family: win.fontRegular; font.pixelSize: 24; color: win.offWhite }
            Text {
                topPadding: 1
                text: (d.album.tracks.length === 1 ? win.tr("1 track") : win.tr("%1 tracks").arg(d.album.tracks.length)) + "  " + win.fmtTime(d.album.duration) + "  " + d.album.year
                font.family: win.fontRegular; font.pixelSize: 18; color: win.offWhite
            }
        }
        Row {
            x: 717; y: 64; spacing: 14
            Image { source: win.res("DETAILS.LEFT.CHEVRON.DISABLED") }
            Image { source: win.res("DETAILS.RIGHT.CHEVRON.NOFOCUS") }
        }
        Image {
            x: 636; y: 208; width: 142; height: 142
            source: d.album.cover || win.res("AUDIO.NOCOVER.1.LARGE")
            fillMode: Image.PreserveAspectCrop
        }
        ListView {
            id: list
            // measured on Windows 7: the command icons at 59, the songs at 84 (66 focused), the lengths ending at 755
            x: 50; y: 272; width: 705; height: parent.height - 300
            clip: true
            model: d.rows
            currentIndex: d.cur
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                width: list.width
                height: index === 1 ? 86 : modelData.cmd ? 36 : 44
                readonly property bool lit: index === d.cur
                Row {
                    visible: !!row.modelData.cmd
                    x: 0; spacing: 10; anchors.verticalCenter: parent.verticalCenter; anchors.verticalCenterOffset: row.index === 1 ? -21 : 0
                    Image {
                        source: win.res(row.modelData.cmd === "add" ? (row.lit ? "DETAILS.CMD.ADD.FOCUS" : "DETAILS.CMD.ADD") : (row.lit ? "DETAILS.CMD.PLAY.FOCUS" : "DETAILS.CMD.PLAY"))
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: row.modelData.cmd === "add" ? win.tr("add to now playing") : win.tr("play album")
                        font.family: row.lit ? win.fontSemibold : win.fontRegular; font.pixelSize: 24
                        color: row.lit ? win.offWhite : win.lightBlue
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    visible: !row.modelData.cmd
                    x: row.lit ? 16 : 34; anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 140; elide: Text.ElideRight
                    text: row.modelData.track ? row.modelData.track.title : ""
                    font.family: row.lit ? win.fontSemibold : win.fontRegular; font.pixelSize: row.lit ? 28 : 24
                    color: row.lit ? win.offWhite : win.lightBlue
                }
                Text {
                    visible: !row.modelData.cmd
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    text: row.modelData.track ? win.fmtTime(row.modelData.track.duration) : ""
                    font.family: row.lit ? win.fontSemibold : win.fontRegular; font.pixelSize: row.lit ? 28 : 24
                    color: row.lit ? win.offWhite : win.lightBlue
                }
                MouseArea { anchors.fill: parent; hoverEnabled: true; onEntered: d.cur = row.index; onClicked: d.run(row.modelData) }
            }
        }
    }
}
