/*
 * A list of songs (BrowseList.mcml, ListViewItem.mcml): the titles 24 pt semibold, light blue at 60 %; the focused
 * one white, 1.16 larger and 18 to the left (FocusLabelMargins); the light strips over and under the list; the
 * focused song's title, artist and rating at the bottom and the counter (measured on Windows 7: rows 70.5 apart).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: sl
    property var songs: []
    signal activated(int index)
    property int current: 0
    onCurrentChanged: win.sound("focus")
    onSongsChanged: current = 0
    readonly property bool lit: activeFocus
    Keys.onDownPressed: { if (current < songs.length - 1) current++ }
    Keys.onUpPressed: e => { if (current > 0) current--; else e.accepted = false }
    Keys.onReturnPressed: if (songs.length) activated(current)
    Keys.onEnterPressed: if (songs.length) activated(current)
    WheelHandler { onWheel: e => sl.current = Math.max(0, Math.min(sl.songs.length - 1, sl.current + (e.angleDelta.y > 0 ? -1 : 1))) }
    // the list scrolls to keep the focused row among the first five
    readonly property int first: Math.max(0, current - 4)

    Image { y: 114; width: sl.width; height: 91; source: win.res("BROWSE.LISTVIEWSTRIPTOP") }
    Image { y: 580; width: sl.width; height: 91; source: win.res("BROWSE.LISTVIEWSTRIP") }

    Item {
        anchors.fill: parent
        opacity: sl.lit ? 1 : 0.75
        Behavior on opacity { NumberAnimation { duration: 300 } }
        Repeater {
            model: sl.songs
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool focused: index === sl.current && sl.lit
                readonly property real cy: 262 + (index - sl.first) * 70.5
                visible: cy > 200 && cy < 560
                y: cy - 35; width: sl.width; height: 70
                Text {
                    x: row.focused ? 207.4 : 225.4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 700
                    elide: Text.ElideRight
                    text: row.modelData.title
                    font.family: win.fontSemibold
                    font.pixelSize: row.focused ? 37 : 32
                    color: row.focused ? win.offWhite : win.lightBlue
                }
                MouseArea {
                    x: 200; width: 760; height: parent.height
                    hoverEnabled: true
                    onEntered: { sl.current = row.index; sl.forceActiveFocus() }
                    onClicked: sl.activated(row.index)
                }
            }
        }
    }

    Column {
        readonly property var s: sl.songs[sl.current] || null
        visible: s !== null && sl.lit
        x: 225; y: 592
        spacing: -3
        Text { text: parent.s ? parent.s.title : ""; font.family: win.fontSemibold; font.pixelSize: 26; color: win.offWhite }
        Text { text: parent.s ? parent.s.artist : ""; font.family: win.fontRegular; font.pixelSize: 24; color: win.lightBlue }
        Row {
            topPadding: 3; spacing: 8
            Repeater { model: 5; delegate: Image { source: win.res("COMMON.STAREMPTY"); width: 22; height: 21 } }
        }
    }
    Pager { current: sl.current + 1; total: sl.songs.length; opacity: sl.lit ? 1 : 0.75 }
}
