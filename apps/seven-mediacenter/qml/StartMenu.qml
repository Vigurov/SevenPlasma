/*
 * The start menu of Windows Media Center (StartMenu.mcml, StartMenuCategory.mcml, StartMenuQuickLink*.mcml):
 * categories in a circle, the selected one with its strip of quick links (focus 258x145, others 162x91 at half
 * transparency), arrows / Enter / mouse / wheel.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

FocusScope {
    id: menu

    readonly property var categories: [
        { title: "Extras", links: [{ t: "extras library", img: "EXTRAS", act: "extras" }] },
        { title: "Pictures + Videos", links: [{ t: "picture library", img: "PICTURES", act: "pictures" },
                                             { t: "play favorites", img: "PICTURES.PLAYFAVS", act: "slideshow" },
                                             { t: "video library", img: "VIDEOS", act: "videos" }] },
        { title: "Music", links: [{ t: "music library", img: "MUSIC", act: "music" },
                                 { t: "play favorites", img: "MUSIC.PLAYFAVS", act: "playfavs" },
                                 { t: "radio", img: "RADIO", act: "radio" },
                                 { t: "search", img: "SEARCH.MUSIC", act: "search" }] },
        { title: "Movies", links: [{ t: "movie library", img: "MOVIES", act: "movies" }] },
        { title: "TV", links: [{ t: "recorded tv", img: "RECORDEDTV", act: "recordedtv" },
                              { t: "live tv setup", img: "SETUPTV", act: "livetvsetup" }] },
        { title: "Tasks", links: [{ t: "shut down", img: "SHUTDOWN", act: "shutdown" },
                                 { t: "settings", img: "SETTINGS", act: "settings" },
                                 { t: "learn more", img: "LEARNMORE", act: "learnmore" },
                                 { t: "burn cd/dvd", img: "BURNCD", act: "burn" },
                                 { t: "sync", img: "SYNC", act: "sync" },
                                 { t: "add extender", img: "ADDEXTENDER", act: "extender" },
                                 { t: "media only", img: "MEDIAONLY", act: "mediaonly" }] }
    ]
    // Media Center opens on TV > recorded tv; the menu remembers where it was
    property int cat: startState.cat
    property var linkOf: startState.links
    readonly property int link: linkOf[cat]
    QtObject {
        id: startState
        property int cat: win.startCat !== undefined ? win.startCat : 4
        property var links: win.startLinks !== undefined ? win.startLinks : [0, 0, 0, 0, 0, 1]
    }
    function remember() { win.startCat = cat; win.startLinks = linkOf }
    function setCat(c) {
        cat = (c + categories.length) % categories.length
        remember()
    }
    function setLink(i) {
        const l = linkOf.slice()
        l[cat] = Math.max(0, Math.min(categories[cat].links.length - 1, i))
        linkOf = l
        remember()
    }

    function activate(act) {
        switch (act) {
        case "extras": win.open({ kind: "extras" }); break
        case "pictures": win.open({ kind: "pictures" }); break
        case "slideshow": {
            let all = []
            for (const f of library.pictureFolders) all = all.concat(f.items)
            if (all.length) win.open({ kind: "picture", items: all, index: 0, slideshow: true })
            break
        }
        case "videos": win.open({ kind: "videos", title: "video library" }); break
        case "movies": win.open({ kind: "videos", title: "movie library" }); break
        case "music": win.open({ kind: "music", pivot: 0 }); break
        case "search": win.open({ kind: "music", pivot: 3 }); break
        case "playfavs":
            if (library.songs.length) { win.shuffle = true; win.playSongs(library.songs, Math.floor(Math.random() * library.songs.length)); win.open({ kind: "nowplaying" }) }
            break
        case "radio": win.open({ kind: "message", title: "radio", text: "To listen to FM radio, a TV tuner with an FM tuner is required." }); break
        case "recordedtv": win.open({ kind: "message", title: "recorded tv", text: "There are no recorded TV shows." }); break
        case "livetvsetup": win.open({ kind: "message", title: "Live TV Setup", text: "Windows Media Center could not find a TV tuner. Connect a TV tuner to this computer, and then try again." }); break
        case "shutdown": win.open({ kind: "message", title: "shut down", power: true }); break
        case "settings": win.open({ kind: "message", title: "settings", settings: true }); break
        case "learnmore": Qt.openUrlExternally("http://go.microsoft.com/fwlink/?LinkId=145765"); break
        case "burn": library.run("seven-dvdmaker"); break
        case "sync":
        case "extender": win.open({ kind: "message", title: act === "sync" ? "sync" : "add extender", text: "This feature is not available on this computer." }); break
        case "mediaonly": win.visibility = win.visibility === 5 ? 2 : 5; break
        }
    }

    focus: true
    Keys.onUpPressed: setCat(cat - 1)
    Keys.onDownPressed: setCat(cat + 1)
    Keys.onLeftPressed: setLink(link - 1)
    Keys.onRightPressed: setLink(link + 1)
    Keys.onReturnPressed: activate(categories[cat].links[link].act)
    Keys.onEnterPressed: activate(categories[cat].links[link].act)

    WheelHandler {
        onWheel: event => menu.setCat(menu.cat + (event.angleDelta.y > 0 ? -1 : 1))
    }

    // ---------------------------------------------------------------- categories
    // the distance of a category from the selected one (-4..+1 are visible)
    function offsetOf(i) {
        const n = categories.length
        let d = (i - cat + n) % n
        if (d > 1) d -= n
        return d
    }
    Repeater {
        model: menu.categories.length
        delegate: Text {
            id: catTitle
            required property int index
            readonly property int d: menu.offsetOf(index)
            readonly property bool selected: d === 0
            text: win.tr(menu.categories[index].title)
            font.family: win.fontLight
            font.pixelSize: 59 // 44 pt of StartMenuCategory.mcml
            color: selected ? win.offWhite : win.lightBlue
            x: selected ? 223 : 243
            // centres measured on Windows 7: 424 selected, 331 / 262 / 192 / 122 above, 715 below
            y: (d === 0 ? 424 : d < 0 ? 331 + (d + 1) * 70 : 715) - height / 2
            opacity: d === 0 ? 1 : d === -1 ? 0.62 : d === -2 ? 0.5 : d === -3 ? 0.4 : d === -4 ? 0.22 : d === 1 ? 0.3 : 0
            visible: opacity > 0
            Behavior on y { enabled: Math.abs(catTitle.d) <= 1 || catTitle.d === -2; NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
            Behavior on opacity { NumberAnimation { duration: 250 } }
            Behavior on x { NumberAnimation { duration: 250 } }
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onClicked: menu.setCat(catTitle.index)
            }
        }
    }

    // ---------------------------------------------------------------- the selected category's quick links
    Item {
        id: strip
        y: 466
        x: 0
        width: menu.width
        height: 200
        readonly property var links: menu.categories[menu.cat].links
        // the focused tile stays at x 230, the others line up around it
        function xOf(i) {
            let x = 230
            if (i < menu.link) {
                for (let k = i; k < menu.link; ++k) x -= 160 + 12
            } else {
                for (let k = menu.link; k < i; ++k) x += k === menu.link ? 258 + 34 : 160 + 12
            }
            return x
        }
        Repeater {
            model: strip.links
            delegate: Item {
                id: tile
                required property var modelData
                required property int index
                readonly property bool focused: index === menu.link
                x: strip.xOf(index)
                width: focused ? 258 : 160
                height: 200
                Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

                Image {
                    id: pic
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: tile.focused ? 0 : 27
                    width: tile.focused ? 258 : 162
                    height: tile.focused ? 145 : 91
                    source: win.res("STARTMENU.QUICKLINK." + tile.modelData.img + (tile.focused ? ".FOCUS" : ".NOFOCUS"))
                    opacity: tile.focused ? 1 : 0.5
                    smooth: true
                }
                // the focus loop (Selector.png: a nine-grid frame) and the glass glow
                BorderImage {
                    visible: tile.focused
                    anchors.fill: pic
                    anchors.margins: -2
                    opacity: 0.75
                    source: win.res("SELECTOR")
                    border { left: 10; top: 10; right: 10; bottom: 10 }
                }
                Image {
                    visible: tile.focused
                    x: pic.x; y: pic.y
                    width: 178; height: 114
                    source: win.res("SELECTOR.FOCUSLOOP.GLOW.GLASS")
                    opacity: 0.35
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: tile.focused ? 150 : 124
                    text: win.tr(tile.modelData.t)
                    font.family: win.fontRegular
                    font.pixelSize: tile.focused ? 31 : 21 // 23 / 16 pt
                    color: win.offWhite
                    opacity: tile.focused ? 1 : 0.5
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: menu.setLink(tile.index)
                    onClicked: menu.activate(tile.modelData.act)
                }
            }
        }
    }
}
