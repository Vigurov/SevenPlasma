/*
 * "Get Started" of the first start: Learn More / Express / Custom (FirstRun.ChooseSetup.*). Express and Custom
 * both finish the setup here (there is no TV to set up without a tuner).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: gs
    readonly property var options: [
        { key: "LEARNMORE", title: "Learn More", head: "", text: "Go online and find out how to get the most from Windows Media Center." },
        { key: "EXPRESS", title: "Express", head: "Set up Windows Media Center with recommended options.", text: "Automatically download media information from the Internet and join the Customer Experience Improvement Program to send data to Microsoft about Windows Media Center's usage and performance." },
        { key: "CUSTOM", title: "Custom", head: "", text: "Set up Windows Media Center by walking through each step of the configuration process, including TV." }
    ]
    property int sel: 1
    function choose(i) {
        if (i === 0) { Qt.openUrlExternally("http://go.microsoft.com/fwlink/?LinkId=145765"); return }
        library.firstRun = false
        win.stack = [{ kind: "start" }]
    }
    Keys.onLeftPressed: sel = Math.max(0, sel - 1)
    Keys.onRightPressed: sel = Math.min(2, sel + 1)
    Keys.onReturnPressed: choose(sel)
    Keys.onEnterPressed: choose(sel)

    Image { anchors.fill: parent; source: win.res("FIRSTRUN.WELCOMECAROUSEL.BACKGROUND") }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 70
        text: win.tr("Get Started")
        font.family: win.fontLight
        font.pixelSize: 64
        color: win.lightBlue
    }
    Repeater {
        model: gs.options.length
        delegate: Item {
            id: opt
            required property int index
            readonly property bool focused: index === gs.sel
            readonly property int size: focused ? 222 : 130
            x: gs.width / 2 + (index - 1) * 290 - size / 2
            y: focused ? 200 : 250
            width: size; height: size + 50
            Behavior on x { NumberAnimation { duration: 250 } }
            Image {
                width: opt.size; height: opt.size
                source: win.res("FIRSTRUN.CHOOSESETUP." + gs.options[opt.index].key)
                smooth: true
                Behavior on width { NumberAnimation { duration: 250 } }
                Behavior on height { NumberAnimation { duration: 250 } }
            }
            Text {
                visible: !opt.focused
                anchors.horizontalCenter: parent.horizontalCenter
                y: opt.size + 4
                text: win.tr(gs.options[opt.index].title)
                font.family: win.fontRegular
                font.pixelSize: 26
                color: win.lightBlue
                opacity: 0.7
            }
            MouseArea { anchors.fill: parent; hoverEnabled: true; onEntered: gs.sel = opt.index; onClicked: gs.choose(opt.index) }
        }
    }
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 440
        width: 860
        spacing: 12
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: win.tr(gs.options[gs.sel].title)
            font.family: win.fontSemibold
            font.pixelSize: 40
            color: win.offWhite
        }
        Text {
            visible: text !== ""
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: gs.options[gs.sel].head === "" ? "" : win.tr(gs.options[gs.sel].head)
            font.family: win.fontSemibold
            font.pixelSize: 22
            color: win.offWhite
        }
        Text {
            width: 700
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: win.tr(gs.options[gs.sel].text)
            font.family: win.fontRegular
            font.pixelSize: 18
            color: win.lightBlue
        }
    }
    WmcButton { x: 36; y: 690; width: 200; text: win.tr("Exit"); onClicked: Qt.quit() }
    WmcButton { x: gs.width - 36 - width; y: 690; text: win.tr("Privacy Statement Online"); onClicked: Qt.openUrlExternally("http://go.microsoft.com/fwlink/?LinkID=57190") }
}
