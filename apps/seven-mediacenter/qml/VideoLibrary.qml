/*
 * video library (PhotosVideos browse pages, "folders" view): the folders and the videos of the Videos folder as a
 * list (BrowseList.mcml, ListViewItem.mcml) - a small picture and the name on each row, the focused row larger; the
 * light strips over and under the list; the focused item's details and the counter at the bottom. A folder opens as
 * a page of its own ("by name | by date"). The movie library keeps its gallery of covers.
 *
 * Positions and sizes measured on Windows 7 (1366x768 canvas): rows 70.5 apart from 251, the name 24 pt semibold at
 * 228, the pictures 110x83 centred at 130 (1.16 larger when focused).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: vl
    readonly property bool movies: win.page.title === "movie library"
    readonly property string folder: win.page.folder || ""          // relative to the Videos folder, "" its top
    readonly property string rootName: win.tr("Videos")
    property int pivot: 0

    // the folders right under this one (with their videos) and the videos in it
    readonly property var entries: {
        const subs = {}, vids = []
        for (const v of library.videos) {
            const f = v.folder === "." ? "" : v.folder
            if (f === vl.folder) { vids.push({ video: v }); continue }
            const inside = vl.folder === "" ? f : f.startsWith(vl.folder + "/") ? f.slice(vl.folder.length + 1) : null
            if (inside === null || inside === "") continue
            const top = inside.split("/")[0]
            const path = vl.folder === "" ? top : vl.folder + "/" + top
            if (!subs[top]) subs[top] = { folder: path, name: top, videos: [] }
            subs[top].videos.push(v)
        }
        const folders = Object.keys(subs).sort((a, b) => a.localeCompare(b)).map(k => subs[k])
        vids.sort((a, b) => vl.pivot === 1 ? b.video.date.split(".").reverse().join("").localeCompare(a.video.date.split(".").reverse().join(""))
                                           : a.video.name.localeCompare(b.video.name))
        return folders.concat(vids)
    }
    function allVideos(e) { return e.video ? [e.video] : e.videos }
    function activate(i) {
        const e = entries[i]
        if (!e) return
        if (e.video) win.open({ kind: "video", video: e.video })
        else win.open({ kind: "videos", title: e.name, folder: e.folder })
    }

    Keys.onUpPressed: header.enterPivots()
    LibraryHeader {
        id: header
        title: vl.folder === "" ? win.tr(win.page.title) : win.page.title
        commands: vl.movies ? [] : [{ t: win.tr("play all"), icon: "BROWSE.CMD.PLAY" }]
        pivots: vl.movies ? [win.tr("title"), win.tr("year"), win.tr("genre")]
                : vl.folder === "" ? [win.tr("folders"), win.tr("date taken"), win.tr("shared")] : [win.tr("by name"), win.tr("by date")]
        pivot: vl.pivot
        onPivotChosen: i => vl.pivot = i
        onCommandChosen: { const all = [].concat(...vl.entries.map(e => vl.allVideos(e))); if (all.length) win.open({ kind: "video", video: all[0] }) }
    }

    // ---------------------------------------------------------------- the movie library: covers
    Gallery {
        visible: vl.movies
        focus: vl.movies
        x: 225; y: 246; width: vl.width - 225; height: 470
        rows: 2
        tileW: 152; tileH: 214
        labelLines: 2
        noImage: "MOVIE.NOCOVER.1.LARGE"
        items: library.videos.map(v => ({ image: v.thumb, text: v.name }))
        onActivated: i => win.open({ kind: "video", video: library.videos[i] })
    }

    // ---------------------------------------------------------------- folders and videos: the list
    Image { visible: list.visible; y: 114; width: vl.width; height: 91; source: win.res("BROWSE.LISTVIEWSTRIPTOP") }
    Image { visible: list.visible; y: 580; width: vl.width; height: 91; source: win.res("BROWSE.LISTVIEWSTRIP") }
    FocusScope {
        id: list
        visible: !vl.movies
        focus: !vl.movies
        anchors.fill: parent
        property int current: 0
        onCurrentChanged: win.sound("focus")
        readonly property bool lit: activeFocus
        Keys.onDownPressed: if (current < vl.entries.length - 1) current++
        Keys.onUpPressed: e => { if (current > 0) current--; else e.accepted = false }
        Keys.onReturnPressed: vl.activate(current)
        Keys.onEnterPressed: vl.activate(current)
        // the list scrolls to keep the focused row among the first five
        readonly property int first: Math.max(0, current - 4)
        opacity: activeFocus ? 1 : 0.75
        Behavior on opacity { NumberAnimation { duration: 300 } }

        Repeater {
            model: vl.entries
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool focused: index === list.current && list.lit
                readonly property real cy: 251 + (index - list.first) * 70.5
                visible: cy > 200 && cy < 560
                x: 0; y: cy - 35
                width: vl.width; height: 70

                // the picture: a folder shows a big and two small pictures on its blue, a video its frame and mark
                Item {
                    id: thumb
                    x: 130 - 55; y: 35 - 41.5
                    width: 110; height: 83
                    transform: Scale { origin.x: 55; origin.y: 41.5; xScale: row.focused ? 1.16 : 1; yScale: xScale
                        Behavior on xScale { NumberAnimation { duration: 150 } } }
                    Rectangle {
                        visible: !row.modelData.video
                        anchors.fill: parent
                        color: "#2e87b0"
                        readonly property var pics: row.modelData.videos ? row.modelData.videos.slice(0, 3).map(v => v.thumb || "") : []
                        Image { x: 4; y: 2; width: 68; height: 53; source: parent.pics[0] || ""; fillMode: Image.PreserveAspectCrop; asynchronous: true }
                        Rectangle { x: 75; y: 2; width: 31; height: 25; color: "#14406e"
                            Image { anchors.fill: parent; source: parent.parent.pics[1] || ""; fillMode: Image.PreserveAspectCrop; asynchronous: true } }
                        Rectangle { x: 75; y: 29; width: 31; height: 26; color: "#14406e"
                            Image { anchors.fill: parent; source: parent.parent.pics[2] || ""; fillMode: Image.PreserveAspectCrop; asynchronous: true } }
                    }
                    Image {
                        visible: !!row.modelData.video
                        anchors.fill: parent
                        source: row.modelData.video ? (row.modelData.video.thumb || "") : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                    Image {
                        visible: !!row.modelData.video
                        anchors { right: parent.right; bottom: parent.bottom; rightMargin: 1; bottomMargin: 1 }
                        source: win.res("BROWSE.VIDEOITEM")
                    }
                    BorderImage {
                        visible: row.focused
                        anchors.fill: parent
                        source: win.res("SELECTOR")
                        border { left: 10; top: 10; right: 10; bottom: 10 }
                    }
                }
                Text {
                    x: 228
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.video ? row.modelData.video.name : row.modelData.name
                    font.family: win.fontSemibold
                    font.pixelSize: row.focused ? 37 : 32 // 24 pt, 1.16 larger when focused
                    color: row.focused ? win.offWhite : win.lightBlue
                    opacity: row.focused ? 1 : 0.6
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: { list.current = row.index; list.forceActiveFocus() }
                    onClicked: vl.activate(row.index)
                }
            }
        }

        // the focused item's details: "Videos : Sample Videos", the date and the number of videos (or the length)
        Column {
            readonly property var e: vl.entries[list.current] || null
            visible: e !== null && list.lit
            x: 225; y: 592
            spacing: -3
            Text {
                text: !parent.e ? "" : parent.e.video ? parent.e.video.name : (vl.folder === "" ? vl.rootName : vl.folder.split("/").pop()) + " : " + parent.e.name
                font.family: win.fontSemibold; font.pixelSize: 26; color: win.offWhite
            }
            Text {
                readonly property var e: parent.e
                text: !e ? "" : e.video ? [e.video.date, e.video.duration ? win.fmtTime(e.video.duration) : ""].filter(x => x).join("   ")
                      : [e.videos[0].date, e.videos.length === 1 ? win.tr("1 item") : win.tr("%1 items").arg(e.videos.length)].join("   ")
                font.family: win.fontRegular; font.pixelSize: 24; color: win.lightBlue
            }
        }
        Pager { current: list.current + 1; total: vl.entries.length }
    }

    Text {
        visible: library.videos.length === 0
        x: 225; y: 260
        text: win.tr("There are no videos in your library. Add videos to the Videos folder.")
        font.family: win.fontRegular; font.pixelSize: 24; color: win.lightBlue
    }
}
