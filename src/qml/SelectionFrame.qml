// SPDX-License-Identifier: GPL-3.0-or-later
// The chosen region's frame: its own x, y, width, height are the region in the overlay's units. The border sits just
// outside the region so it never covers a chosen pixel; the 8 handles are centred on the corners and edge middles.
// It takes no pointer input: the overlay's MouseArea below does the hit-testing through the controller.
pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: frame

    property color colour: "#e9a046"
    property color handleFill: "#f0ece0"
    property int borderWidth: 2
    property int handleSize: 10

    Rectangle {
        x: -frame.borderWidth
        y: -frame.borderWidth
        width: frame.width + 2 * frame.borderWidth
        height: frame.height + 2 * frame.borderWidth
        color: "transparent"
        border.width: frame.borderWidth
        border.color: frame.colour
    }

    Repeater {
        // Corners and edge middles as fractions of the frame: TopLeft, Top, TopRight, Right, BottomRight, Bottom,
        // BottomLeft, Left.
        model: [[0, 0], [0.5, 0], [1, 0], [1, 0.5], [1, 1], [0.5, 1], [0, 1], [0, 0.5]]
        Rectangle {
            required property var modelData
            width: frame.handleSize
            height: frame.handleSize
            radius: width / 2
            x: Math.round(frame.width * modelData[0]) - width / 2
            y: Math.round(frame.height * modelData[1]) - height / 2
            color: frame.handleFill
            border.width: 2
            border.color: frame.colour
            antialiasing: true
        }
    }
}
