/*
 * The first start of Media Center: the welcome carousel (FR.WelcomeScreen.mcml, FirstRun.WelcomeCarousel.*) with
 * the cards of a computer without a TV guide, then Continue.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: welcome
    readonly property var cards: [
        { img: 1, title: "Welcome to Windows Media Center", text: "The best way to experience TV on your PC." },
        { img: 3, title: "Your PC Is Your DVR", text: "Add a TV tuner and record your favorite TV shows to watch whenever you want." },
        { img: 4, title: "Enjoy Your Movies", text: "Use the Movie Library to access all the movies from your PC and DVD player from one location." },
        { img: 5, title: "On Your TV", text: "View Windows Media Center on your TV with an Xbox 360." },
        { img: 6, title: "Everything in One Place", text: "Let Windows Media Center organize your vacation photos, home movies, music, and slide shows." }
    ]
    property int index: 0
    function step(d) { index = (index + d + cards.length) % cards.length }
    Keys.onLeftPressed: step(-1)
    Keys.onRightPressed: step(1)
    Keys.onReturnPressed: win.stack = [{ kind: "getstarted" }]
    Keys.onEnterPressed: win.stack = [{ kind: "getstarted" }]
    Timer { interval: 7000; running: true; repeat: true; onTriggered: welcome.step(1) }

    Image { anchors.fill: parent; source: win.res("FIRSTRUN.WELCOMECAROUSEL.BACKGROUND") }

    // the neighbouring cards, small and to the side, the current one in the middle
    Repeater {
        model: welcome.cards.length
        delegate: Image {
            id: card
            required property int index
            readonly property int d: {
                const n = welcome.cards.length
                let x = (index - welcome.index + n) % n
                return x > n / 2 ? x - n : x
            }
            source: win.res("FIRSTRUN.WELCOMECAROUSEL.IMAGE" + welcome.cards[index].img)
            width: d === 0 ? 572 : 343
            height: d === 0 ? 450 : 270
            x: welcome.width / 2 - width / 2 + d * 520
            y: d === 0 ? 70 : 160
            opacity: d === 0 ? 1 : Math.abs(d) === 1 ? 0.55 : 0
            visible: opacity > 0
            z: d === 0 ? 2 : 1
            smooth: true
            Behavior on x { NumberAnimation { duration: 500; easing.type: Easing.OutCubic } }
            Behavior on width { NumberAnimation { duration: 500; easing.type: Easing.OutCubic } }
            Behavior on height { NumberAnimation { duration: 500; easing.type: Easing.OutCubic } }
            Behavior on y { NumberAnimation { duration: 500; easing.type: Easing.OutCubic } }
            Behavior on opacity { NumberAnimation { duration: 500 } }
        }
    }

    component Arrow: Image {
        signal clicked()
        MouseArea { anchors.fill: parent; onClicked: parent.clicked() }
    }
    Arrow { source: win.res("PIVOT.ARROW.LEFT"); x: 120; y: 455; onClicked: welcome.step(-1) }
    Arrow { source: win.res("PIVOT.ARROW.RIGHT"); x: welcome.width - 120 - width; y: 455; onClicked: welcome.step(1) }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 515
        text: win.tr(welcome.cards[welcome.index].title)
        font.family: win.fontLight
        font.pixelSize: 44
        color: win.offWhite
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        width: 900
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        y: 588
        text: win.tr(welcome.cards[welcome.index].text)
        font.family: win.fontRegular
        font.pixelSize: 22
        color: win.lightBlue
    }
    WmcButton {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 668
        text: win.tr("Continue")
        onClicked: win.stack = [{ kind: "getstarted" }]
    }
}
