// SPDX-License-Identifier: GPL-3.0-or-later
// The floating capture bar, in the GNOME style: a rounded panel with the mode buttons (Region, Screen, Window), the
// big round capture button, the "include pointer" toggle and a close button. Colours and font come from the Riley
// theme (controller.theme). Its size comes from its content only and nothing animates, so its geometry is final on
// the first frame. The shadow reaches at most 12 logical pixels past the bar's own rect.
pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: bar

    required property CaptureController controller
    readonly property var theme: controller ? controller.theme : ({})

    readonly property color baseColour: theme.base !== undefined ? theme.base : "#151a23"
    readonly property color surfaceColour: theme.surface0 !== undefined ? theme.surface0 : "#1d2430"
    readonly property color edgeColour: theme.surface1 !== undefined ? theme.surface1 : "#283244"
    readonly property color textColour: theme.text !== undefined ? theme.text : "#f0ece0"
    readonly property color accentColour: theme.accent !== undefined ? theme.accent : "#e9a046"

    readonly property int padding: 12

    implicitWidth: content.implicitWidth + 2 * padding
    implicitHeight: content.implicitHeight + 2 * padding
    width: implicitWidth
    height: implicitHeight

    // Shadow: three soft layers, the widest 12 px past the bar (8 px spread + 4 px drop).
    Repeater {
        model: [{ grow: 12, drop: 0, alpha: 0.10 }, { grow: 8, drop: 2, alpha: 0.14 }, { grow: 4, drop: 2, alpha: 0.20 }]
        Rectangle {
            required property var modelData
            x: -modelData.grow + 0
            y: -modelData.grow + modelData.drop
            width: bar.width + 2 * modelData.grow
            height: bar.height + 2 * modelData.grow - modelData.drop
            radius: panel.radius + modelData.grow
            color: Qt.rgba(0, 0, 0, modelData.alpha)
        }
    }

    Rectangle {
        id: panel
        anchors.fill: parent
        radius: 24
        color: bar.surfaceColour
        border.width: 1
        border.color: bar.edgeColour
    }

    // Clicks and hovers on the panel stay on the panel: they never reach the overlay below.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
        cursorShape: Qt.ArrowCursor
    }

    Row {
        id: content
        x: bar.padding
        y: bar.padding
        spacing: 16

        // The mode group.
        Rectangle {
            width: modes.implicitWidth + 8
            height: modes.implicitHeight + 8
            anchors.verticalCenter: parent.verticalCenter
            radius: 16
            color: bar.baseColour

            Row {
                id: modes
                x: 4
                y: 4
                spacing: 4

                BarButton {
                    objectName: "modeRegion"
                    theme: bar.theme
                    icon: "region"
                    label: qsTr("Region")
                    checked: bar.controller.mode === CaptureController.Region
                    onClicked: bar.controller.mode = CaptureController.Region
                }
                BarButton {
                    objectName: "modeScreen"
                    theme: bar.theme
                    icon: "screen"
                    label: qsTr("Screen")
                    checked: bar.controller.mode === CaptureController.Screen
                    onClicked: bar.controller.mode = CaptureController.Screen
                }
                BarButton {
                    objectName: "modeWindow"
                    theme: bar.theme
                    icon: "window"
                    label: qsTr("Window")
                    checked: bar.controller.mode === CaptureController.Window
                    onClicked: bar.controller.mode = CaptureController.Window
                }
            }
        }

        // The big round capture button: a ring around a filled disc.
        Item {
            id: captureButton
            objectName: "captureButton"
            width: 64
            height: 64
            anchors.verticalCenter: parent.verticalCenter
            enabled: bar.controller.canCapture
            opacity: enabled ? 1.0 : 0.4

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.width: 4
                border.color: bar.textColour
            }
            Rectangle {
                anchors.centerIn: parent
                width: captureMouse.pressed ? 44 : 50
                height: width
                radius: width / 2
                color: captureMouse.containsMouse ? bar.accentColour : bar.textColour
            }
            MouseArea {
                id: captureMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: bar.controller.capture()
            }
        }

        BarButton {
            objectName: "pointerToggle"
            anchors.verticalCenter: parent.verticalCenter
            theme: bar.theme
            icon: "pointer"
            label: qsTr("Pointer")
            checked: bar.controller.pointer
            onClicked: bar.controller.pointer = !bar.controller.pointer
        }

        BarButton {
            objectName: "closeButton"
            anchors.verticalCenter: parent.verticalCenter
            theme: bar.theme
            icon: "close"
            round: true
            onClicked: bar.controller.cancel()
        }
    }
}
