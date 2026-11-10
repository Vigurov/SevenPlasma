/*
 * The Bluetooth icon of the Windows 7 notification area (bthprops.cpl, menu 2140): a click opens the adapter
 * menu, the tooltip lists the connected devices with their battery charge.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.plasma5support as P5Support
import org.kde.kirigami as Kirigami
import org.kde.bluezqt as BluezQt

PlasmoidItem {
    id: root

    readonly property var manager: BluezQt.Manager
    readonly property var adapter: manager.usableAdapter
    readonly property bool hasAdapter: manager.adapters.length > 0
    readonly property bool adapterOn: !manager.bluetoothBlocked && adapter !== null && adapter.powered
    property var deviceLines: []

    function refresh() {
        const lines = [];
        const devices = manager.devices;
        for (let i = 0; i < devices.length; ++i) {
            const d = devices[i];
            if (!d.connected) {
                continue;
            }
            lines.push(d.battery ? d.name + ": заряд " + d.battery.percentage + " %" : d.name + ": підключено");
        }
        deviceLines = lines;
    }

    function run(command) {
        runner.connectSource(command);
    }

    P5Support.DataSource {
        id: runner
        engine: "executable"
        connectedSources: []
        onNewData: source => disconnectSource(source)
    }

    Timer {
        interval: 5000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: root.refresh()
    }
    Connections {
        target: root.manager
        function onDevicesChanged() { root.refresh(); }
        function onDeviceChanged() { root.refresh(); }
    }

    Plasmoid.title: "Пристрої Bluetooth"
    Plasmoid.icon: "seven-tray-bluetooth"
    // Windows shows the icon while there is an adapter; "Remove Icon" moves it among the hidden ones
    Plasmoid.status: !hasAdapter ? PlasmaCore.Types.HiddenStatus
                     : (Plasmoid.configuration.iconRemoved ? PlasmaCore.Types.PassiveStatus : PlasmaCore.Types.ActiveStatus)

    toolTipMainText: "Пристрої Bluetooth"
    toolTipSubText: {
        if (!hasAdapter) {
            return "Адаптер Bluetooth не знайдено";
        }
        if (!adapterOn) {
            return "Адаптер Bluetooth вимкнуто";
        }
        return deviceLines.join("\n");
    }

    preferredRepresentation: compactRepresentation
    compactRepresentation: MouseArea {
        id: compact
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        onPressed: mouse => menu.openFor(compact)

        Kirigami.Icon {
            anchors.centerIn: parent
            width: Kirigami.Units.iconSizes.roundedIconSize(Math.min(parent.width, parent.height))
            height: width
            source: "seven-tray-bluetooth"
            active: compact.containsMouse
            opacity: root.adapterOn ? 1 : 0.5
        }
    }
    fullRepresentation: Item {}

    PlasmaExtras.Menu {
        id: menu
        placement: PlasmaExtras.Menu.TopPosedLeftAlignedPopup

        function openFor(item) {
            visualParent = item;
            openRelative();
        }

        PlasmaExtras.MenuItem {
            text: "Додати пристрій"
            enabled: root.adapterOn
            onClicked: root.run("seven-control --add-device")
        }
        PlasmaExtras.MenuItem {
            text: "Дозволити підключення пристрою"
            enabled: root.adapterOn
            // what Windows' option does: the computer becomes visible to other devices for a while
            onClicked: {
                root.adapter.pairable = true;
                root.adapter.discoverable = true;
            }
        }
        PlasmaExtras.MenuItem {
            text: "Показати пристрої Bluetooth"
            onClicked: root.run("seven-control --devices")
        }
        PlasmaExtras.MenuItem { separator: true }
        PlasmaExtras.MenuItem {
            text: "Надіслати файл"
            enabled: root.adapterOn
            onClicked: root.run("bluedevil-sendfile")
        }
        PlasmaExtras.MenuItem {
            text: "Отримати файл"
            enabled: root.adapterOn
            onClicked: root.run("kcmshell6 kcm_bluetooth")
        }
        PlasmaExtras.MenuItem { separator: true }
        PlasmaExtras.MenuItem {
            text: "Приєднатися до особистої мережі"
            enabled: false
        }
        PlasmaExtras.MenuItem { separator: true }
        PlasmaExtras.MenuItem {
            text: "Відкрити параметри"
            onClicked: root.run("kcmshell6 kcm_bluetooth")
        }
        PlasmaExtras.MenuItem { separator: true }
        PlasmaExtras.MenuItem {
            text: root.manager.bluetoothBlocked ? "Увімкнути адаптер" : "Вимкнути адаптер"
            onClicked: root.manager.bluetoothBlocked = !root.manager.bluetoothBlocked
        }
        PlasmaExtras.MenuItem { separator: true }
        PlasmaExtras.MenuItem {
            text: Plasmoid.configuration.iconRemoved ? "Показати значок" : "Видалити значок"
            onClicked: Plasmoid.configuration.iconRemoved = !Plasmoid.configuration.iconRemoved
        }
    }
}
