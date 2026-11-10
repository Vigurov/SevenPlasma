/*
 * Windows 7 taskbar toolbars: "Desktop" (a » menu of the desktop), "Address" (type a path or address)
 * and toolbars made from any folder ("New toolbar...").
 */
import QtQuick
import QtQuick.Layouts
import QtCore
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.components as PC3
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.plasma5support as P5Support
import org.kde.kirigami as Kirigami

PlasmoidItem {
    id: root

    readonly property string kind: Plasmoid.configuration.kind
    readonly property string home: StandardPaths.writableLocation(StandardPaths.HomeLocation).toString().replace("file://", "")
    readonly property string folder: kind === "desktop" ? StandardPaths.writableLocation(StandardPaths.DesktopLocation).toString().replace("file://", "")
                                                        : String(Plasmoid.configuration.folder).replace("file://", "")
    readonly property string label: kind === "address" ? "Адреса" : kind === "desktop" ? "Робочий стіл" : folder.split("/").filter(s => s.length).pop() || "/"

    Plasmoid.title: label
    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
    preferredRepresentation: fullRepresentation

    function quote(s) {
        return "'" + String(s).replace(/'/g, "'\\''") + "'";
    }
    function open(path, isDir) {
        if (isDir)
            exec.run("dolphin " + quote(path));
        else if (path.endsWith(".desktop"))
            exec.run("kioclient exec " + quote(path));
        else
            exec.run("xdg-open " + quote(path));
    }

    P5Support.DataSource {
        id: exec
        engine: "executable"
        connectedSources: []
        property var pending: ({})
        function run(cmd, callback) {
            if (callback)
                pending[cmd] = callback;
            connectSource(cmd);
        }
        onNewData: (source, data) => {
            const cb = pending[source];
            delete pending[source];
            disconnectSource(source);
            if (cb)
                cb(data["stdout"]);
        }
    }

    ListModel {
        id: menuModel
    }

    function showMenu() {
        exec.run("python3 " + quote(Qt.resolvedUrl("../code/list.py").toString().replace("file://", "")) + " " + quote(folder) + " #" + Date.now(), out => {
            menuModel.clear();
            if (kind === "desktop") { // the Windows 7 desktop namespace comes first
                menuModel.append({ name: home.split("/").pop(), icon: "user-home", path: home, dir: true, sep: false });
                menuModel.append({ name: "Комп'ютер", icon: "computer", path: "file:///", dir: true, sep: false });
                menuModel.append({ name: "Мережа", icon: "folder-network", path: "remote:/", dir: true, sep: false });
                menuModel.append({ name: "Панель керування", icon: "preferences-system", path: "seven-control", dir: false, sep: false });
                menuModel.append({ name: "Смітник", icon: "user-trash", path: "trash:/", dir: true, sep: false });
                menuModel.append({ name: "", icon: "", path: "", dir: false, sep: true });
            }
            let items = [];
            try { items = JSON.parse(out); } catch (e) {}
            const shown = {};
            for (let i = 0; i < menuModel.count; ++i)
                shown[menuModel.get(i).name] = true;
            for (const it of items) {
                if (kind === "desktop" && it.path.endsWith(".desktop") && shown[it.name])
                    continue; // desktop icons like Computer or Recycle Bin are already listed above
                menuModel.append({ name: it.name, icon: it.icon, path: it.path, dir: it.dir, sep: false });
            }
            if (menuModel.count === 0)
                menuModel.append({ name: "(Пусто)", icon: "", path: "", dir: false, sep: false });
            folderMenu.openRelative();
        });
    }

    PlasmaExtras.Menu {
        id: folderMenu
        visualParent: button
        placement: PlasmaExtras.Menu.TopPosedLeftAlignedPopup
    }
    Instantiator {
        model: menuModel
        delegate: PlasmaExtras.MenuItem {
            required property var model
            text: model.name
            icon: model.icon
            separator: model.sep
            enabled: model.path !== ""
            onClicked: {
                if (model.path.endsWith("seven-control"))
                    exec.run(model.path);
                else if (model.path.indexOf(":/") > 0)
                    exec.run("dolphin " + root.quote(model.path));
                else
                    root.open(model.path, model.dir);
            }
        }
        onObjectAdded: (index, object) => folderMenu.addMenuItem(object)
        onObjectRemoved: (index, object) => folderMenu.removeMenuItem(object)
    }

    fullRepresentation: Item {
        Layout.minimumWidth: root.kind === "address" ? 250 : button.implicitWidth
        Layout.preferredWidth: Layout.minimumWidth
        Layout.fillHeight: true

        // folder / desktop: "Name »"
        MouseArea {
            id: button
            visible: root.kind !== "address"
            anchors.fill: parent
            implicitWidth: row.implicitWidth + 12
            hoverEnabled: true
            onClicked: root.showMenu()
            Rectangle {
                anchors.fill: parent
                anchors.margins: 3
                radius: 2
                visible: button.containsMouse || folderMenu.status === PlasmaExtras.Menu.Open
                color: button.pressed ? "#33ffffff" : "#22ffffff"
                border.color: "#55ffffff"
            }
            Row {
                id: row
                anchors.centerIn: parent
                spacing: 4
                Text {
                    text: root.label
                    color: "white"
                    font.pixelSize: 12
                    style: Text.Raised
                    styleColor: "#80000000"
                }
                Text {
                    text: "»"
                    color: "white"
                    font.pixelSize: 13
                    font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: -1
                }
            }
        }

        // address: "Адреса [__________]"
        RowLayout {
            visible: root.kind === "address"
            anchors.fill: parent
            anchors.leftMargin: 4
            anchors.rightMargin: 2
            spacing: 5
            Text {
                text: "Адреса"
                color: "white"
                font.pixelSize: 12
                style: Text.Raised
                styleColor: "#80000000"
            }
            PC3.TextField {
                id: address
                Layout.fillWidth: true
                Layout.preferredHeight: 22
                font.pixelSize: 12
                color: "black"
                leftPadding: 4
                background: Rectangle {
                    color: "white"
                    border.color: address.activeFocus ? "#3d7bad" : "#8e8f8f"
                    radius: 1
                }
                onAccepted: {
                    const t = text.trim();
                    if (!t.length)
                        return;
                    if (t.startsWith("/") || t.startsWith("~"))
                        exec.run("dolphin " + root.quote(t.replace(/^~/, root.home)));
                    else if (t.indexOf("://") > 0)
                        exec.run("xdg-open " + root.quote(t));
                    else if (/^[^\s]+\.[a-z]{2,}(\/.*)?$/i.test(t))
                        exec.run("xdg-open " + root.quote("https://" + t));
                    else
                        exec.run(t); // like the Run box
                    text = "";
                }
            }
        }
    }
}
