/*
 * The simple pages: a title and a message (recorded tv, live tv setup, radio...), the power menu of "shut down"
 * and "settings".
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: mp
    readonly property var p: win.page
    readonly property var power: [
        { t: "Close", run: () => Qt.quit() },
        { t: "Log Off", run: () => library.run("qdbus6", ["org.kde.Shutdown", "/Shutdown", "logout"]) },
        { t: "Shut Down", run: () => library.run("qdbus6", ["org.kde.Shutdown", "/Shutdown", "logoutAndShutdown"]) },
        { t: "Restart", run: () => library.run("qdbus6", ["org.kde.Shutdown", "/Shutdown", "logoutAndReboot"]) },
        { t: "Sleep", run: () => library.run("systemctl", ["suspend"]) }
    ]
    readonly property var settings: [
        { t: "General", text: "Start-up and window behavior, visual and sound effects, program library options." },
        { t: "TV", text: "To set up TV, a TV tuner must be connected to this computer." },
        { t: "Pictures", text: "Pictures come from your Pictures folder:", extra: "\n" + library.picturesFolder },
        { t: "Music", text: "Music comes from your Music folder:", extra: "\n" + library.musicFolder },
        { t: "DVD", text: "DVDs play in the movie library when a disc is in the drive." },
        { t: "Media Libraries", text: "The libraries show the Music, Pictures and Videos folders:", extra: "\n" + library.musicFolder + "\n" + library.picturesFolder + "\n" + library.videosFolder }
    ]
    readonly property var buttons: p.power ? power : p.settings ? settings : []
    property int cur: 0
    property int detailIndex: 0
    readonly property string detail: p.settings ? win.tr(settings[detailIndex].text) + (settings[detailIndex].extra || "") : ""
    Keys.onUpPressed: { cur = Math.max(0, cur - 1); detailIndex = cur }
    Keys.onDownPressed: { cur = Math.min(buttons.length - 1, cur + 1); detailIndex = cur }
    Keys.onReturnPressed: if (p.power) power[cur].run()

    Text {
        anchors { right: parent.right; rightMargin: 56 }
        y: 52
        text: win.tr(mp.p.title)
        font.family: win.fontLight; font.pixelSize: 58
        color: win.lightBlue; opacity: 0.45
    }
    Text {
        visible: !!mp.p.text
        x: 225; y: 230; width: 900
        wrapMode: Text.WordWrap
        text: mp.p.text ? win.tr(mp.p.text) : ""
        font.family: win.fontRegular; font.pixelSize: 26
        color: win.offWhite
    }
    Column {
        x: 225; y: 200
        spacing: 8
        Repeater {
            model: mp.buttons
            delegate: WmcButton {
                required property var modelData
                required property int index
                width: 320
                text: win.tr(modelData.t)
                focused: index === mp.cur
                onClicked: { mp.cur = index; mp.detailIndex = index; if (mp.p.power) modelData.run() }
            }
        }
    }
    Text {
        visible: !!mp.p.settings
        x: 600; y: 205; width: 640
        wrapMode: Text.WordWrap
        text: mp.detail
        font.family: win.fontRegular; font.pixelSize: 22
        color: win.lightBlue
    }
}
