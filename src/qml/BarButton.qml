// SPDX-License-Identifier: GPL-3.0-or-later
// One button of the capture bar: a rounded tile with a small drawn icon and an optional label. `checked` marks the
// current mode or an active toggle. No QtQuick.Controls: a plain MouseArea, so a click never takes the keyboard
// focus away from the overlay.
pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: button

    // The bar's theme map (CaptureController.theme).
    property var theme: ({})
    // "region", "screen", "window", "pointer" or "close".
    property string icon: ""
    property string label: ""
    property bool checked: false
    // A round button (the close button) instead of a rounded tile.
    property bool round: false

    signal clicked()

    readonly property color textColour: theme.text !== undefined ? theme.text : "#f0ece0"
    readonly property color accentColour: theme.accent !== undefined ? theme.accent : "#e9a046"
    readonly property color surfaceColour: theme.surface1 !== undefined ? theme.surface1 : "#283244"
    readonly property color baseColour: theme.base !== undefined ? theme.base : "#151a23"
    readonly property color iconColour: checked ? baseColour : textColour

    implicitWidth: round ? 32 : Math.max(64, labelText.implicitWidth + 24)
    implicitHeight: round ? 32 : 56

    opacity: enabled ? 1.0 : 0.4

    Rectangle {
        anchors.fill: parent
        radius: button.round ? width / 2 : 12
        color: button.checked ? button.accentColour
                              : (mouse.containsMouse ? button.surfaceColour : Qt.alpha(button.surfaceColour, 0.0))
    }

    Item {
        id: glyph
        width: 20
        height: 16
        anchors.horizontalCenter: parent.horizontalCenter
        y: button.round ? (parent.height - height) / 2 : 10

        // Region: a dashed rectangle drawn as corner brackets.
        Item {
            anchors.fill: parent
            visible: button.icon === "region"
            Repeater {
                model: [[0, 0, 7, 2], [0, 0, 2, 6], [13, 0, 7, 2], [18, 0, 2, 6],
                        [0, 14, 7, 2], [0, 10, 2, 6], [13, 14, 7, 2], [18, 10, 2, 6]]
                Rectangle {
                    required property var modelData
                    x: modelData[0]; y: modelData[1]; width: modelData[2]; height: modelData[3]
                    color: button.iconColour
                }
            }
        }

        // Screen: a monitor on a stand.
        Item {
            anchors.fill: parent
            visible: button.icon === "screen"
            Rectangle {
                x: 0; y: 0; width: 20; height: 12; radius: 2
                color: "transparent"
                border.width: 2
                border.color: button.iconColour
            }
            Rectangle { x: 9; y: 12; width: 2; height: 2; color: button.iconColour }
            Rectangle { x: 5; y: 14; width: 10; height: 2; radius: 1; color: button.iconColour }
        }

        // Window: a frame with a title bar.
        Item {
            anchors.fill: parent
            visible: button.icon === "window"
            Rectangle {
                anchors.fill: parent
                radius: 2
                color: "transparent"
                border.width: 2
                border.color: button.iconColour
            }
            Rectangle { x: 0; y: 0; width: 20; height: 5; radius: 2; color: button.iconColour }
        }

        // Pointer: an arrow.
        Canvas {
            anchors.centerIn: parent
            width: 16
            height: 18
            visible: button.icon === "pointer"
            property color ink: button.iconColour
            onInkChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                ctx.fillStyle = ink;
                ctx.beginPath();
                ctx.moveTo(2, 1);
                ctx.lineTo(2, 15);
                ctx.lineTo(6, 11.5);
                ctx.lineTo(8.5, 17);
                ctx.lineTo(11, 16);
                ctx.lineTo(8.5, 10.5);
                ctx.lineTo(13.5, 10.5);
                ctx.closePath();
                ctx.fill();
            }
        }

        // Close: a cross.
        Item {
            anchors.centerIn: parent
            width: 14
            height: 14
            visible: button.icon === "close"
            Rectangle {
                anchors.centerIn: parent
                width: 16; height: 2; radius: 1
                rotation: 45
                antialiasing: true
                color: button.iconColour
            }
            Rectangle {
                anchors.centerIn: parent
                width: 16; height: 2; radius: 1
                rotation: -45
                antialiasing: true
                color: button.iconColour
            }
        }
    }

    Text {
        id: labelText
        visible: !button.round && text !== ""
        text: button.label
        anchors.horizontalCenter: parent.horizontalCenter
        y: glyph.y + glyph.height + 6
        color: button.iconColour
        font.family: button.theme.font !== undefined ? button.theme.font : "IBM Plex Mono"
        font.pixelSize: 12
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: button.clicked()
    }
}
