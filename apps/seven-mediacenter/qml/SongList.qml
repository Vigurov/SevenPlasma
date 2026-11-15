/*
 * A list of songs (Audio.SongListItem.xml): title, artist and length; the focused row lit.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: sl
    property var songs: []
    property bool showArtist: true
    signal activated(int index)
    Keys.onUpPressed: list.decrementCurrentIndex()
    Keys.onDownPressed: list.incrementCurrentIndex()
    Keys.onReturnPressed: if (songs.length) activated(list.currentIndex)
    Keys.onEnterPressed: if (songs.length) activated(list.currentIndex)

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        model: sl.songs
        highlightMoveDuration: 150
        onCurrentIndexChanged: win.sound("focus")
        boundsBehavior: Flickable.StopAtBounds
        delegate: Item {
            id: row
            required property var modelData
            required property int index
            width: list.width
            height: 40
            readonly property bool lit: ListView.isCurrentItem && sl.activeFocus
            BorderImage {
                visible: row.lit
                anchors.fill: parent
                source: win.res("COMMON.BUTTON.LEFT.FOCUS")
                border { left: 12; top: 12; right: 12; bottom: 12 }
            }
            Text {
                x: 16; anchors.verticalCenter: parent.verticalCenter
                width: parent.width * 0.5
                elide: Text.ElideRight
                text: row.modelData.title
                font.family: win.fontRegular; font.pixelSize: 22
                color: row.lit ? win.offWhite : win.lightBlue
            }
            Text {
                visible: sl.showArtist
                x: parent.width * 0.55; anchors.verticalCenter: parent.verticalCenter
                width: parent.width * 0.33
                elide: Text.ElideRight
                text: row.modelData.artist
                font.family: win.fontRegular; font.pixelSize: 20
                color: win.lightBlue
                opacity: 0.8
            }
            Text {
                anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                text: win.fmtTime(row.modelData.duration)
                font.family: win.fontRegular; font.pixelSize: 20
                color: win.lightBlue
            }
            MouseArea {
                anchors.fill: parent; hoverEnabled: true
                onEntered: { list.currentIndex = row.index; sl.forceActiveFocus() }
                onClicked: sl.activated(row.index)
            }
        }
    }
}
