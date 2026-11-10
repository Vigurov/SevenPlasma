/*
 *   Copyright 2014 Marco Martin <mart@kde.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2,
 *   or (at your option) any later version, as published by the Free
 *   Software Foundation
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details
 *
 *   You should have received a copy of the GNU General Public
 *   License along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Templates
import org.kde.plasma.core as PlasmaCore
import QtMultimedia
import QtCore
import "../components"

import aeroshell.utils as AeroShellUtils

Item {
    id: root
    z: -9

    property int stage

    onStageChanged: {
        if (stage == 5) {
            //lockSuccess.play();

            //fadeOut.running = true;
            // Windows 7: "Welcome" itself dissolves into the real desktop. KWin's loginaero effect fades
            // this window out when it closes, so nothing is drawn over it here (no black, no wallpaper picture)
        }
    }
    /*MediaPlayer {
        id: lockSuccess
        source: Qt.resolvedUrl("../sounds/lockSuccess.ogg");
        audioOutput: AudioOutput {}
    }*/

    Rectangle {
        color: "#1D5F7A"
        anchors.fill: parent
    }

    AeroShellUtils.SDDM { id: sddm }

    Image {
        id: bgtexture
        source: sddm.currentBackground
        anchors.fill: parent
    }

    Status {
        id: statusText
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -36
        statusText: "Ласкаво просимо"
        speen: true
    }

    RowLayout {
        anchors {
            bottom: parent.bottom
            left: parent.left
            right: parent.right
        }
        height: 96
        Rectangle { Layout.fillWidth: true }
        Image {
            id: watermark
            source: "../images/watermark.png"
        }
        Rectangle { Layout.fillWidth: true }
    }

    // Windows 7 goes from "Welcome" straight to the desktop: fade into the desktop wallpaper, not to black
    // (~/.local/share/seven-aero/splash-wallpaper points at the current wallpaper; black if it is missing)
    Rectangle {
        id: transitionAnim
        opacity: 0
        color: "black"
        anchors.fill: parent
        Image {
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            source: StandardPaths.writableLocation(StandardPaths.GenericDataLocation) + "/seven-aero/splash-wallpaper"
            asynchronous: false
        }
        Behavior on opacity {
            NumberAnimation { duration: 420; easing.type: Easing.InOutQuad }   // as in the Windows 7 video: about 0.4 s
        }
    }

    /*OpacityAnimator {
        id: fadeOut
        running: false
        target: transitionAnim
        from: 0
        to: 1
        duration: 640
        easing.type: Easing.InOutQuad
    }*/
}
