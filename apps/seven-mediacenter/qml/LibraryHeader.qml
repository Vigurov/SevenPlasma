/*
 * The top of a library page (BrowsePage.mcml, BrowseCommand.mcml, BrowsePivots.mcml, BrowsePivotItem.mcml): the big
 * title at the right, the commands (play all, search...) and the pivots ("< albums >  artists  genres ...").
 *
 * The page's focus is in one of three areas, like Windows 7's FocusArea: the content (gallery or list), the pivots
 * or the commands. Up from the content goes to the pivots, Up again to the commands; the mouse moves the focus to
 * what it is over. The colours, sizes and gaps are those of the MCML files:
 *   - pivots: 18 pt regular, the chosen one 22 pt semibold; 62 between items, 220 where the list starts again (it
 *     goes round when there are four or more); the chosen one stays at the left, the arrows around it; the row
 *     grows 6 % (from its bottom left) while it has the focus
 *   - commands: 18 pt regular; the list grows 6 % (from 25 % / bottom) while one of them has the focus
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick
import QtQuick.Window

FocusScope {
    id: h
    property string title
    property var commands: []   // [{t, icon}]
    property var pivots: []
    property int pivot: 0
    signal pivotChosen(int i)
    signal commandChosen(int i)
    anchors.fill: parent

    // ---------------------------------------------------------------- focus areas
    property int cmd: -1                     // the focused command while the commands have the focus
    readonly property string area: !activeFocus ? "content" : cmd >= 0 ? "command" : "pivot"
    property Item back: null                 // the content item to go back to
    function enter() {
        if (!activeFocus) { back = Window.activeFocusItem; forceActiveFocus() }
    }
    function enterPivots() { enter(); cmd = -1 }
    function enterCommand(i) { enter(); if (cmd !== i) win.sound("focus"); cmd = i }
    // the page's content items (the first visible one takes the focus back), else the item that had it
    property var targets: []
    function leave() {
        cmd = -1
        const b = targets.find(t => t && t.visible) || back
        if (b) Qt.callLater(() => b.forceActiveFocus())
    }
    onActiveFocusChanged: if (!activeFocus) cmd = -1
    Keys.onUpPressed: if (commands.length) enterCommand(cmd < 0 ? commands.length - 1 : Math.max(0, cmd - 1))
    Keys.onDownPressed: {
        if (cmd >= 0 && cmd < commands.length - 1) enterCommand(cmd + 1)
        else if (cmd >= 0) { win.sound("focus"); cmd = -1 }
        else leave()
    }
    Keys.onLeftPressed: if (cmd < 0) step(-1)
    Keys.onRightPressed: if (cmd < 0) step(1)
    Keys.onReturnPressed: if (cmd >= 0) { win.sound("select"); commandChosen(cmd) }
    Keys.onEnterPressed: if (cmd >= 0) { win.sound("select"); commandChosen(cmd) }

    // ---------------------------------------------------------------- title
    Text {
        anchors { right: parent.right; rightMargin: 55 }
        y: 34
        text: h.title
        font.family: win.fontLight
        font.pixelSize: 63
        color: win.cyan
        opacity: h.area === "content" ? 0.62 : 0.24
        Behavior on opacity { NumberAnimation { duration: 300 } }
    }

    // ---------------------------------------------------------------- commands
    Column {
        id: cmdList
        x: 184; y: 131 - h.commands.length * 34
        transform: Scale {
            origin.x: cmdList.width * 0.25; origin.y: cmdList.height
            xScale: h.area === "command" ? 1.06 : 1; yScale: xScale
            Behavior on xScale { NumberAnimation { duration: 300; easing.type: Easing.OutQuad } }
        }
        Repeater {
            model: h.commands
            // a command: focused it turns white, its icon becomes the framed button and the focus loop plays on it
            // (the glow at the top left, the spark running along the top, the blue light at the bottom; 3 s loop)
            delegate: Item {
                id: c
                required property var modelData
                required property int index
                readonly property bool lit: h.area === "command" && h.cmd === index
                width: row.width; height: 34
                Row {
                    id: row
                    spacing: 10
                    height: parent.height
                    Item {
                        id: icon
                        width: 29; height: 24
                        anchors.verticalCenter: parent.verticalCenter
                        Image {
                            visible: c.lit
                            x: -42; y: -22; width: icon.width + 67; height: icon.height + 34
                            source: win.res("BROWSE.CMD.FOCUSLOOP.TOPLEFT")
                            SequentialAnimation on opacity {
                                running: c.lit; loops: Animation.Infinite
                                NumberAnimation { from: 0; to: 0; duration: 267 }
                                NumberAnimation { to: 0.2; duration: 1000; easing.type: Easing.OutQuad }
                                NumberAnimation { to: 0; duration: 1300; easing.type: Easing.InQuad }
                                PauseAnimation { duration: 433 }
                            }
                        }
                        Image { source: win.res(c.modelData.icon + (c.lit ? ".FOCUS" : h.area === "command" ? "" : ".DORMANT")) }
                        Image {
                            visible: c.lit
                            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 2 }
                            source: win.res("BROWSE.CMD.FOCUSLOOP.BLUE")
                            SequentialAnimation on opacity {
                                running: c.lit; loops: Animation.Infinite
                                NumberAnimation { from: 0; to: 0.5; duration: 1133; easing.type: Easing.InOutSine }
                                NumberAnimation { to: 0; duration: 1434; easing.type: Easing.InOutSine }
                                PauseAnimation { duration: 433 }
                            }
                        }
                        Image {
                            visible: c.lit
                            y: 2
                            source: win.res("BROWSE.CMD.FOCUSLOOP.SMALL")
                            SequentialAnimation on x {
                                running: c.lit; loops: Animation.Infinite
                                NumberAnimation { from: -7; to: -7; duration: 167 }
                                NumberAnimation { to: 10; duration: 2400 }
                                PauseAnimation { duration: 433 }
                            }
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: c.modelData.t
                        font.family: win.fontRegular
                        font.pixelSize: 24
                        color: c.lit ? win.offWhite : h.area === "command" ? win.lightBlue : win.cyan
                    }
                }
                MouseArea {
                    anchors.fill: parent; hoverEnabled: true
                    onEntered: h.enterCommand(c.index)
                    onClicked: { win.sound("select"); h.commandChosen(c.index) }
                }
            }
        }
    }

    // ---------------------------------------------------------------- pivots
    readonly property int n: pivots.length
    // the list goes round when it has four or more items and doesn't fit (from the chosen one to the right edge)
    readonly property bool loops: n >= 4 && loopLength - 220 > width - textX
    readonly property int textX: 225               // where the chosen pivot's text starts
    // widths of the items: the chosen one in its bigger font
    property var widths: []
    function measure() {
        const w = []
        for (let i = 0; i < n; i++) w.push(i === pivot ? semiMetrics.advanceOf(pivots[i]) : regMetrics.advanceOf(pivots[i]))
        widths = w
    }
    TextMetrics { id: regMetrics; font.family: win.fontRegular; font.pixelSize: 24; font.letterSpacing: -0.2
        function advanceOf(s) { text = s || ""; return advanceWidth } }
    TextMetrics { id: semiMetrics; font.family: win.fontSemibold; font.pixelSize: 29; font.letterSpacing: 0.25
        function advanceOf(s) { text = s || ""; return advanceWidth } }
    onPivotsChanged: measure()
    Component.onCompleted: { measure(); scrollAnim.enabled = false; scroll = posOf(0, pivot); scrollAnim.enabled = true }
    // the position of item i of copy c on the endless line of pivots
    readonly property real loopLength: { let s = 0; for (const w of widths) s += w + 65.6; return s - 65.6 + 220 }
    function posOf(c, i) { let s = c * loopLength; for (let j = 0; j < i; j++) s += widths[j] + 65.6; return s }
    property int targetCopy: 0
    property real scroll: 0
    Behavior on scroll { id: scrollAnim; NumberAnimation { id: scrollNum; duration: 500; easing.type: Easing.OutQuad } }
    onPivotChanged: { measure(); scroll = posOf(targetCopy, pivot) }
    function copyNow() { return loops && loopLength > 0 ? Math.round((scroll - posOf(0, pivot)) / loopLength) : 0 }
    function step(d) {
        if (!n) return
        let c = copyNow(), j = pivot + d
        if (j >= n) { if (!loops) return; j = 0; c++ }
        if (j < 0) { if (!loops) return; j = n - 1; c-- }
        win.sound("focus")
        targetCopy = c
        pivotChosen(j)
    }
    function chooseCopy(c, i) {
        if (i === pivot && c === copyNow()) return
        win.sound("mini")
        targetCopy = c
        pivotChosen(i)
    }
    // keep the copies near the middle when the list has gone round
    Timer {
        interval: 600; running: h.loops && Math.abs(h.copyNow()) > 0
        onTriggered: {
            if (scrollNum.running) { restart(); return }   // not while it is moving
            const c = h.copyNow(); scrollAnim.enabled = false; h.targetCopy -= c; h.scroll -= c * h.loopLength; scrollAnim.enabled = true
        }
    }

    Item {
        id: pivotPanel
        x: 0; y: 136
        width: h.width; height: 40
        // the row grows from its bottom left (the left arrow) while the pivots have the focus
        transform: Scale {
            origin.x: 200; origin.y: pivotPanel.height
            xScale: h.area === "pivot" ? 1.06 : 1; yScale: xScale
            Behavior on xScale { NumberAnimation { duration: 300; easing.type: Easing.OutQuad } }
        }
        HoverHandler { onHoveredChanged: if (hovered && h.area !== "pivot") h.enterPivots() }

        Repeater {
            model: h.loops ? h.n * 3 : h.n
            delegate: Text {
                id: pv
                required property int index
                readonly property int copy: h.loops ? Math.floor(index / h.n) - 1 : 0
                readonly property int item: index % h.n
                // every copy of the chosen pivot looks chosen (only the one at the left is on screen)
                readonly property bool sel: item === h.pivot
                text: h.pivots[item] || ""
                x: h.textX + h.posOf(copy, item) - h.scroll
                anchors.verticalCenter: parent.verticalCenter
                visible: x + width > -20 && x < h.width + 20
                // the edges of the row fade (50, 10 while the pivots have the focus)
                readonly property real fade: h.area === "pivot" ? 10 : 50
                opacity: Math.max(0, Math.min(1, Math.min((x + width) / fade, (h.width - x) / fade)))
                font.family: sel ? win.fontSemibold : win.fontRegular
                font.pixelSize: sel ? 29 : 24
                // matched to the widths of Windows 7's words (its text is laid out a little differently)
                font.letterSpacing: sel ? 0.25 : -0.2
                color: pvMouse.containsMouse || (sel && h.area === "pivot") ? win.offWhite
                       : h.area === "pivot" ? win.lightBlue
                       : sel && h.area === "content" ? win.lightBlue : win.cyan
                MouseArea { id: pvMouse; anchors.fill: parent; hoverEnabled: true; onClicked: h.chooseCopy(pv.copy, pv.item) }
            }
        }
        // the arrows around the chosen pivot: dormant while the content has the focus, white with the pivots
        // (dim at an end of a list that doesn't go round), light blue while a command has it
        function arrow(side, hasMore) {
            return win.res("PIVOT.ARROW." + side + (h.area === "content" ? ".DORMANT" : h.area === "command" ? ".NOFOCUS" : hasMore ? "" : ".DIM"))
        }
        Image {
            visible: h.n > 0
            x: h.textX - 23
            anchors.verticalCenter: parent.verticalCenter
            source: pivotPanel.arrow("LEFT", h.loops || h.pivot > 0)
            MouseArea { anchors.fill: parent; anchors.margins: -8; onClicked: h.step(-1) }
        }
        Image {
            visible: h.n > 0
            x: h.textX + (h.widths[h.pivot] || 0) + 12
            anchors.verticalCenter: parent.verticalCenter
            source: pivotPanel.arrow("RIGHT", h.loops || h.pivot < h.n - 1)
            MouseArea { anchors.fill: parent; anchors.margins: -8; onClicked: h.step(1) }
        }
    }
}
