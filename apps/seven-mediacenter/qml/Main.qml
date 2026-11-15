/*
 * Windows Media Center: the window, the 1366x768 canvas of the "ten foot" interface (scaled to the window like the
 * original), the page stack, the player, the keys of the remote / keyboard and the mouse controls.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick
import QtQuick.Window
import QtMultimedia

Window {
    id: win
    width: 1100
    height: 660
    minimumWidth: 640
    minimumHeight: 400
    visible: true
    color: "black"
    title: qsTranslate("Win7", "Windows Media Center")
    visibility: startFullScreen ? Window.FullScreen : Window.Windowed

    // Colors.mcml / FontNames.mcml
    readonly property color offWhite: "#f2f2f2"
    readonly property color lightBlue: "#97d9ff"
    readonly property color yellow: "#ffff64"
    readonly property string fontRegular: "Segoe Media Center"
    readonly property string fontLight: "Segoe Media Center Light"
    readonly property string fontSemibold: "Segoe Media Center Semibold"
    function res(name) { return library.res + name.toUpperCase() + ".PNG" }
    function tr(s) { return qsTranslate("Win7", s) }
    function fmtTime(sec) {
        sec = Math.max(0, Math.floor(sec))
        const h = Math.floor(sec / 3600), m = Math.floor(sec / 60) % 60, s = sec % 60
        return (h > 0 ? h + ":" + (m < 10 ? "0" : "") : "") + m + ":" + (s < 10 ? "0" : "") + s
    }

    // ---------------------------------------------------------------- playback (one player for music and video)
    property var queue: []          // songs of "now playing"
    property int queueIndex: -1
    property bool shuffle: false
    property bool repeat: false
    readonly property var current: queueIndex >= 0 && queueIndex < queue.length ? queue[queueIndex] : null
    readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
    readonly property bool hasMedia: player.source != ""
    property bool videoMode: false
    property var startCat      // the start menu's place, kept while other pages are open
    property var startLinks

    function playSongs(list, start) {
        videoMode = false
        queue = list
        queueIndex = start || 0
        player.source = queue[queueIndex].url
        player.play()
    }
    function addToQueue(list) {
        if (queue.length === 0) { playSongs(list, 0); return }
        queue = queue.concat(list)
    }
    function playVideo(url) {
        queue = []
        queueIndex = -1
        videoMode = true
        player.source = url
        player.play()
    }
    function next() {
        if (videoMode || queue.length === 0) return
        let i = shuffle ? Math.floor(Math.random() * queue.length) : queueIndex + 1
        if (i >= queue.length) { if (!repeat) { player.stop(); return } i = 0 }
        queueIndex = i
        player.source = queue[i].url
        player.play()
    }
    function previous() {
        if (player.position > 3000 || queueIndex <= 0) { player.position = 0; return }
        queueIndex--
        player.source = queue[queueIndex].url
        player.play()
    }
    function togglePlay() {
        if (!hasMedia) return
        playing ? player.pause() : player.play()
    }
    function stop() {
        player.stop()
        if (videoMode) { videoMode = false; player.source = "" }
    }

    MediaPlayer {
        id: player
        audioOutput: AudioOutput { id: audio; volume: 0.8 }
        videoOutput: videoSink
        onMediaStatusChanged: if (mediaStatus === MediaPlayer.EndOfMedia) win.videoMode ? win.goBack() : win.next()
    }

    // ---------------------------------------------------------------- pages
    // each page: {kind, ...}; the start menu is the bottom of the stack
    property var stack: [{ kind: library.firstRun ? "welcome" : "start" }]
    readonly property var page: stack[stack.length - 1]
    function open(p) { stack = stack.concat([p]) }
    function goBack() {
        if (page.kind === "video") { stop() }
        if (stack.length > 1) { stack = stack.slice(0, stack.length - 1) }
    }
    function goHome() {
        if (page.kind === "video") { stop() }
        stack = [{ kind: "start" }]
    }

    // the virtual canvas: 768 units high, as wide as the window's proportions allow (widescreen aware)
    Item {
        id: canvas
        readonly property real s: win.height / 768
        width: win.width / s
        height: 768
        scale: s
        transformOrigin: Item.TopLeft

        PageBackground {
            anchors.fill: parent
            visible: !["video", "picture", "nowplaying", "welcome", "getstarted"].includes(win.page.kind)
        }

        VideoOutput {
            id: videoSink
            anchors.fill: parent
            visible: win.videoMode && win.page.kind === "video"
            fillMode: VideoOutput.PreserveAspectFit
        }

        Loader {
            id: pageLoader
            anchors.fill: parent
            focus: true
            source: {
                switch (win.page.kind) {
                case "welcome": return "Welcome.qml"
                case "getstarted": return "GetStarted.qml"
                case "start": return "StartMenu.qml"
                case "music": return "MusicLibrary.qml"
                case "album": return "AlbumDetails.qml"
                case "nowplaying": return "NowPlaying.qml"
                case "pictures": return "PictureLibrary.qml"
                case "folder": return "PictureFolder.qml"
                case "picture": return "PictureViewer.qml"
                case "videos": return "VideoLibrary.qml"
                case "video": return "VideoPage.qml"
                case "extras": return "ExtrasLibrary.qml"
                default: return "MessagePage.qml"
                }
            }
            onLoaded: item.forceActiveFocus()
        }

        // the clock and the logo of the top right corner (not over full-screen pictures and video)
        Row {
            visible: ["start", "music", "pictures", "videos", "extras", "album", "folder", "message"].includes(win.page.kind)
            anchors { top: parent.top; right: parent.right; topMargin: 13; rightMargin: 42 }
            spacing: 10
            Text {
                id: clock
                anchors.verticalCenter: parent.verticalCenter
                font.family: win.fontRegular
                font.pixelSize: 22
                color: win.offWhite
                text: Qt.formatTime(new Date(), Qt.locale().timeFormat(Locale.ShortFormat))
                Timer { interval: 10000; running: true; repeat: true; onTriggered: clock.text = Qt.formatTime(new Date(), Qt.locale().timeFormat(Locale.ShortFormat)) }
            }
            Image { source: win.res("STARTMENU.LOGO.SMALL"); width: 32; height: 32; anchors.verticalCenter: parent.verticalCenter }
        }

        // the "now playing" inset of the menus: the album and a stop button, bottom left
        Item {
            visible: win.hasMedia && !win.videoMode && !["nowplaying", "welcome", "getstarted", "picture", "start"].includes(win.page.kind)
            x: 36; y: 612; width: 116; height: 116
            Image {
                anchors.fill: parent
                source: win.current && win.current.cover ? win.current.cover : win.res("AUDIO.NOCOVER.1.SMALL")
                fillMode: Image.PreserveAspectCrop
            }
            MouseArea { anchors.fill: parent; onClicked: win.open({ kind: "nowplaying" }) }
        }

    }

    // the mouse controls are not scaled with the canvas
    MouseChrome { id: chrome; anchors.fill: parent }

    // ---------------------------------------------------------------- keys of the remote control (keyboard shortcuts)
    Shortcut { sequences: ["Backspace", "Escape", "Back"]; onActivated: win.goBack() }
    Shortcut { sequence: "Ctrl+P"; onActivated: win.togglePlay() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: win.stop() }
    Shortcut { sequence: "Ctrl+F"; onActivated: win.next() }
    Shortcut { sequence: "Ctrl+B"; onActivated: win.previous() }
    Shortcut { sequence: "Ctrl+Shift+F"; onActivated: player.position = player.position + 30000 }
    Shortcut { sequence: "Ctrl+Shift+B"; onActivated: player.position = Math.max(0, player.position - 10000) }
    Shortcut { sequence: "F8"; onActivated: audio.muted = !audio.muted }
    Shortcut { sequence: "F9"; onActivated: audio.volume = Math.max(0, audio.volume - 0.05) }
    Shortcut { sequence: "F10"; onActivated: audio.volume = Math.min(1, audio.volume + 0.05) }
    Shortcut { sequences: ["Alt+Return", "F11"]; onActivated: win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
    Shortcut { sequence: "Ctrl+M"; onActivated: win.open({ kind: "nowplaying" }) }
    Shortcut { sequences: ["Meta+Alt+Return", "Home"]; onActivated: win.goHome() }
    Shortcut { sequence: "MediaPlay"; onActivated: win.togglePlay() }
    Shortcut { sequence: "MediaTogglePlayPause"; onActivated: win.togglePlay() }
    Shortcut { sequence: "MediaStop"; onActivated: win.stop() }
    Shortcut { sequence: "MediaNext"; onActivated: win.next() }
    Shortcut { sequence: "MediaPrevious"; onActivated: win.previous() }

    // exposed to pages
    property alias mediaPlayer: player
    property alias audioOutput: audio
}
