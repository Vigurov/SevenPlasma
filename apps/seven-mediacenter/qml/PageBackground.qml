/*
 * The background of Media Center's pages (PageBackground.mcml): Windows 7 shows StaticBackground,
 * Common.Background.png stretched to the window. Screenshots of a real Windows 7 match it pixel for pixel; the
 * AnimatedBackground (darker picture with light layers) isn't what it shows.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick

Item {
    clip: true
    Image {
        anchors.fill: parent
        source: win.res("COMMON.BACKGROUND")
        smooth: true
    }
}
