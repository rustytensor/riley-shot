// SPDX-License-Identifier: GPL-3.0-or-later
// The toolbar's colours: the 8 theme swatches ("swatch0" ... "swatch7", from the editor's palette) and a custom
// colour button ("customColour") that opens a colour dialog. Every choice sets document.colour, which is the
// current tool's own colour. The swatch matching the current colour wears a ring.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

Row {
    id: picker

    property var document: null
    property var colours: []
    property var theme: ({})

    readonly property color ringColour: theme.text !== undefined ? theme.text : "#f0ece0"
    readonly property color edgeColour: theme.surface1 !== undefined ? theme.surface1 : "#283244"
    readonly property color current: document ? document.colour : "transparent"

    spacing: 4

    function same(a, b) {
        return Qt.colorEqual(a, b);
    }

    component Swatch: Item {
        id: swatch
        required property int index
        readonly property color colour: picker.colours.length > index ? picker.colours[index] : "transparent"
        readonly property bool chosen: picker.same(colour, picker.current)

        objectName: "swatch" + index
        width: 24
        height: 36

        Rectangle {
            anchors.centerIn: parent
            width: 24
            height: 24
            radius: 12
            color: "transparent"
            border.width: 2
            border.color: swatch.chosen ? picker.ringColour
                                        : (swatchMouse.containsMouse ? picker.edgeColour : "transparent")
        }
        Rectangle {
            anchors.centerIn: parent
            width: 16
            height: 16
            radius: 8
            color: swatch.colour
        }
        MouseArea {
            id: swatchMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (picker.document)
                    picker.document.colour = swatch.colour;
            }
        }
        ToolTip.visible: swatchMouse.containsMouse
        ToolTip.delay: 700
        ToolTip.text: swatch.colour.toString()
    }

    Swatch { index: 0 }
    Swatch { index: 1 }
    Swatch { index: 2 }
    Swatch { index: 3 }
    Swatch { index: 4 }
    Swatch { index: 5 }
    Swatch { index: 6 }
    Swatch { index: 7 }

    // A custom colour: a ring of hues around the current colour when it is not one of the swatches.
    Item {
        id: custom
        objectName: "customColour"
        width: 28
        height: 36

        readonly property bool chosen: {
            for (let i = 0; i < picker.colours.length; ++i)
                if (picker.same(picker.colours[i], picker.current))
                    return false;
            return true;
        }

        Canvas {
            id: hues
            anchors.centerIn: parent
            width: 24
            height: 24
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                const c = width / 2;
                for (let i = 0; i < 12; ++i) {
                    ctx.beginPath();
                    ctx.strokeStyle = Qt.hsla(i / 12, 0.7, 0.6, 1);
                    ctx.lineWidth = 3;
                    ctx.arc(c, c, c - 2, i / 12 * 2 * Math.PI, (i + 1) / 12 * 2 * Math.PI + 0.05);
                    ctx.stroke();
                }
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: 12
            height: 12
            radius: 6
            color: custom.chosen ? picker.current : "transparent"
            border.width: custom.chosen ? 0 : 1
            border.color: picker.ringColour
        }
        MouseArea {
            id: customMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                colourDialog.selectedColor = picker.current;
                colourDialog.open();
            }
        }
        ToolTip.visible: customMouse.containsMouse
        ToolTip.delay: 700
        ToolTip.text: qsTr("Custom colour")
    }

    ColorDialog {
        id: colourDialog
        title: qsTr("Colour")
        onAccepted: {
            if (picker.document)
                picker.document.colour = selectedColor;
        }
    }
}
