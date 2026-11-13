// SPDX-License-Identifier: GPL-3.0-or-later
// The editor's top toolbar, in the Riley theme: the tools, the fill toggle, the colours, the stroke width, undo
// and redo, copy, save and save as, and zoom. It wraps onto a second row when the window is narrow. The objectNames
// are the contract in src/app/editorwindow.h. Buttons are plain items with a MouseArea, so a click never takes the
// keyboard focus away from the editor.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Rectangle {
    id: toolbar

    property Document document: null
    property DocumentCanvas canvas: null
    property var theme: ({})
    property var colours: []

    signal saveAsRequested()
    signal copied(bool ok)

    readonly property color baseColour: theme.base !== undefined ? theme.base : "#151a23"
    readonly property color surfaceColour: theme.surface0 !== undefined ? theme.surface0 : "#1d2430"
    readonly property color edgeColour: theme.surface1 !== undefined ? theme.surface1 : "#283244"
    readonly property color textColour: theme.text !== undefined ? theme.text : "#f0ece0"
    readonly property color subtextColour: theme.subtext !== undefined ? theme.subtext : "#d6d2c4"
    readonly property color accentColour: theme.accent !== undefined ? theme.accent : "#e9a046"
    readonly property string fontFamily: theme.font !== undefined ? theme.font : "IBM Plex Mono"

    readonly property int padding: 8

    // Draws one 20x20 icon in colour ink.
    function drawIcon(ctx, name, ink) {
        ctx.strokeStyle = ink;
        ctx.fillStyle = ink;
        ctx.lineWidth = 2;
        ctx.lineCap = "round";
        ctx.lineJoin = "round";
        const line = (x1, y1, x2, y2) => { ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke(); };
        const poly = (pts, fill) => {
            ctx.beginPath();
            ctx.moveTo(pts[0][0], pts[0][1]);
            for (let i = 1; i < pts.length; ++i)
                ctx.lineTo(pts[i][0], pts[i][1]);
            ctx.closePath();
            if (fill) ctx.fill(); else ctx.stroke();
        };
        const curvedArrow = (flip) => {
            ctx.save();
            if (flip) { ctx.translate(20, 0); ctx.scale(-1, 1); }
            ctx.beginPath();
            ctx.moveTo(5, 8);
            ctx.lineTo(12, 8);
            ctx.arc(12, 12.5, 4.5, -Math.PI / 2, Math.PI / 2, false);
            ctx.lineTo(7, 17);
            ctx.stroke();
            poly([[2, 8], [7, 4], [7, 12]], true);
            ctx.restore();
        };
        const magnifier = () => {
            ctx.beginPath(); ctx.arc(8.5, 8.5, 6, 0, 2 * Math.PI); ctx.stroke();
            ctx.lineWidth = 2.5;
            line(13, 13, 18, 18);
            ctx.lineWidth = 2;
        };
        switch (name) {
        case "select":
            poly([[5, 2], [5, 17], [9, 13.5], [11.5, 18.5], [14, 17.5], [11.5, 12.5], [16, 12.5]], true);
            break;
        case "pen":
            poly([[3, 17], [4, 13], [14, 3], [17, 6], [7, 16]], false);
            line(12, 5, 15, 8);
            break;
        case "highlighter":
            ctx.globalAlpha = 0.45;
            ctx.fillRect(2, 13, 16, 5);
            ctx.globalAlpha = 1;
            poly([[6, 13], [6, 9], [12, 3], [16, 7], [10, 13]], false);
            break;
        case "eraser":
            poly([[2, 13], [10, 5], [17, 12], [12, 17], [6, 17]], false);
            poly([[2, 13], [6, 9], [11, 14], [8, 17], [6, 17]], true);
            line(9, 18, 18, 18);
            break;
        case "line":
            line(3, 17, 17, 3);
            break;
        case "arrow":
            line(3, 17, 13, 7);
            poly([[17, 3], [15.5, 11], [9, 4.5]], true);
            break;
        case "rectangle":
            ctx.strokeRect(3, 5, 14, 10);
            break;
        case "ellipse":
            ctx.beginPath(); ctx.ellipse(2.5, 4.5, 15, 11); ctx.stroke();
            break;
        case "fill":
            ctx.strokeRect(3, 4, 14, 12);
            poly([[3, 16], [17, 4], [17, 16]], true);
            break;
        case "text":
            line(4, 4, 16, 4);
            line(10, 4, 10, 17);
            line(7, 17, 13, 17);
            break;
        case "marker":
            ctx.beginPath(); ctx.arc(10, 10, 8, 0, 2 * Math.PI); ctx.fill();
            ctx.strokeStyle = toolbar.surfaceColour;
            line(8, 7, 10.5, 5.5);
            line(10.5, 5.5, 10.5, 14.5);
            break;
        case "crop":
            line(5, 1, 5, 15); line(5, 15, 19, 15);
            line(1, 5, 15, 5); line(15, 5, 15, 19);
            break;
        case "blur":
            for (const [x, y, r, a] of [[10, 10, 3, 1], [4, 5, 1.5, 0.6], [16, 5, 1.5, 0.6], [4, 15, 1.5, 0.6],
                                        [16, 15, 1.5, 0.6], [10, 3, 1.5, 0.8], [10, 17, 1.5, 0.8], [3, 10, 1.5, 0.8],
                                        [17, 10, 1.5, 0.8]]) {
                ctx.globalAlpha = a;
                ctx.beginPath(); ctx.arc(x, y, r, 0, 2 * Math.PI); ctx.fill();
            }
            ctx.globalAlpha = 1;
            break;
        case "pixelate":
            for (let i = 0; i < 3; ++i)
                for (let j = 0; j < 3; ++j)
                    if ((i + j) % 2 === 0)
                        ctx.fillRect(2 + i * 5.5, 2 + j * 5.5, 5, 5);
            ctx.lineWidth = 1;
            ctx.strokeRect(2, 2, 16, 16);
            break;
        case "undo":
            curvedArrow(false);
            break;
        case "redo":
            curvedArrow(true);
            break;
        case "copy":
            ctx.strokeRect(6, 6, 11, 12);
            poly([[3, 14], [3, 2], [13, 2]], false);
            break;
        case "save":
            line(10, 2, 10, 12);
            poly([[6, 9], [10, 13.5], [14, 9]], false);
            line(3, 13, 3, 18); line(3, 18, 17, 18); line(17, 18, 17, 13);
            break;
        case "saveas":
            line(8, 2, 8, 11);
            poly([[4.5, 8], [8, 12], [11.5, 8]], false);
            line(2, 12, 2, 18); line(2, 18, 11, 18);
            poly([[13, 18], [13.5, 15], [18, 10.5], [20, 12.5], [15.5, 17]], true);
            break;
        case "zoomin":
            magnifier();
            line(6, 8.5, 11, 8.5); line(8.5, 6, 8.5, 11);
            break;
        case "zoomout":
            magnifier();
            line(6, 8.5, 11, 8.5);
            break;
        case "fit":
            line(2, 7, 2, 2); line(2, 2, 7, 2);
            line(13, 2, 18, 2); line(18, 2, 18, 7);
            line(2, 13, 2, 18); line(2, 18, 7, 18);
            line(13, 18, 18, 18); line(18, 18, 18, 13);
            ctx.strokeRect(6.5, 6.5, 7, 7);
            break;
        }
    }

    color: surfaceColour
    implicitHeight: flow.implicitHeight + 2 * padding
    height: implicitHeight

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: toolbar.edgeColour
    }

    // One square button with a drawn icon. `checked` marks the current tool or an active toggle.
    component IconButton: Item {
        id: button
        property string icon: ""
        property string tip: ""
        property bool checked: false
        signal clicked()

        width: 34
        height: 34
        opacity: enabled ? 1.0 : 0.35

        readonly property color ink: checked ? toolbar.baseColour : toolbar.textColour

        Rectangle {
            anchors.fill: parent
            radius: 8
            color: button.checked ? toolbar.accentColour
                                  : (mouse.containsMouse && button.enabled ? toolbar.edgeColour : "transparent")
        }
        Canvas {
            id: glyph
            anchors.centerIn: parent
            width: 20
            height: 20
            property color ink: button.ink
            onInkChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                toolbar.drawIcon(ctx, button.icon, ink);
            }
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: button.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: button.clicked()
        }
        ToolTip.visible: mouse.containsMouse && button.tip !== ""
        ToolTip.delay: 600
        ToolTip.text: button.tip
    }

    // A tool button: checked while its tool is current, a click makes it current.
    component ToolButton: IconButton {
        id: toolButton
        required property int tool
        checked: toolbar.document !== null && toolbar.document.tool === tool
        onClicked: {
            if (toolbar.document)
                toolbar.document.tool = toolButton.tool;
        }
    }

    // A rounded group behind a row of controls.
    component Group: Rectangle {
        default property alias content: row.data
        width: row.implicitWidth + 8
        height: 42
        radius: 10
        color: toolbar.baseColour
        Row {
            id: row
            x: 4
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
        }
    }

    component Divider: Rectangle {
        width: 1
        height: 22
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        color: toolbar.edgeColour
    }

    Flow {
        id: flow
        x: toolbar.padding
        y: toolbar.padding
        width: toolbar.width - 2 * toolbar.padding
        spacing: 8

        Group {
            ToolButton { objectName: "tool_select"; tool: Document.Select; icon: "select"; tip: qsTr("Select and move") }
            Divider {}
            ToolButton { objectName: "tool_pen"; tool: Document.Pen; icon: "pen"; tip: qsTr("Pen") }
            ToolButton { objectName: "tool_highlighter"; tool: Document.Highlighter; icon: "highlighter"; tip: qsTr("Highlighter") }
            ToolButton { objectName: "tool_eraser"; tool: Document.Eraser; icon: "eraser"; tip: qsTr("Eraser") }
            Divider {}
            ToolButton { objectName: "tool_line"; tool: Document.Line; icon: "line"; tip: qsTr("Line") }
            ToolButton { objectName: "tool_arrow"; tool: Document.Arrow; icon: "arrow"; tip: qsTr("Arrow") }
            ToolButton { objectName: "tool_rectangle"; tool: Document.Rectangle; icon: "rectangle"; tip: qsTr("Rectangle") }
            ToolButton { objectName: "tool_ellipse"; tool: Document.Ellipse; icon: "ellipse"; tip: qsTr("Ellipse") }
            IconButton {
                objectName: "fillToggle"
                icon: "fill"
                tip: qsTr("Fill rectangles and ellipses")
                checked: toolbar.document !== null && toolbar.document.filled
                onClicked: {
                    if (toolbar.document)
                        toolbar.document.filled = !toolbar.document.filled;
                }
            }
            Divider {}
            ToolButton { objectName: "tool_text"; tool: Document.Text; icon: "text"; tip: qsTr("Text") }
            ToolButton { objectName: "tool_marker"; tool: Document.Marker; icon: "marker"; tip: qsTr("Numbered marker") }
            Divider {}
            ToolButton { objectName: "tool_crop"; tool: Document.Crop; icon: "crop"; tip: qsTr("Crop") }
            ToolButton { objectName: "tool_blur"; tool: Document.Blur; icon: "blur"; tip: qsTr("Blur") }
            ToolButton { objectName: "tool_pixelate"; tool: Document.Pixelate; icon: "pixelate"; tip: qsTr("Pixelate") }
        }

        Group {
            ColourPicker {
                document: toolbar.document
                colours: toolbar.colours
                theme: toolbar.theme
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            }
            Divider {}
            // The stroke width, 1 to 32 image pixels: drag or click on the track, or turn the wheel over it.
            Item {
                id: strokeWidth
                objectName: "strokeWidth"
                readonly property real minimum: 1
                readonly property real maximum: 32
                readonly property real value: toolbar.document ? toolbar.document.strokeWidth : 4
                width: 132
                height: 34

                function setFrom(x) {
                    if (!toolbar.document)
                        return;
                    const f = Math.max(0, Math.min(1, (x - track.x) / track.width));
                    toolbar.document.strokeWidth = Math.round(minimum + f * (maximum - minimum));
                }

                Item {
                    id: dot
                    width: 20
                    height: parent.height
                    Rectangle {
                        anchors.centerIn: parent
                        readonly property real d: Math.max(2, Math.min(18, strokeWidth.value * 0.6 + 1))
                        width: d
                        height: d
                        radius: d / 2
                        color: toolbar.document ? toolbar.document.colour : toolbar.textColour
                    }
                }
                Rectangle {
                    id: track
                    x: dot.width + 6
                    width: parent.width - dot.width - 6 - valueText.width - 6
                    height: 4
                    radius: 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: toolbar.edgeColour
                    Rectangle {
                        width: parent.width * (strokeWidth.value - strokeWidth.minimum)
                               / (strokeWidth.maximum - strokeWidth.minimum)
                        height: parent.height
                        radius: 2
                        color: toolbar.accentColour
                    }
                    Rectangle {
                        x: parent.width * (strokeWidth.value - strokeWidth.minimum)
                           / (strokeWidth.maximum - strokeWidth.minimum) - width / 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: 12
                        height: 12
                        radius: 6
                        color: toolbar.textColour
                    }
                }
                Text {
                    id: valueText
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 22
                    horizontalAlignment: Text.AlignRight
                    text: Math.round(strokeWidth.value)
                    color: toolbar.subtextColour
                    font.family: toolbar.fontFamily
                    font.pixelSize: 12
                }
                MouseArea {
                    id: strokeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onPressed: (mouse) => strokeWidth.setFrom(mouse.x)
                    onPositionChanged: (mouse) => {
                        if (pressed)
                            strokeWidth.setFrom(mouse.x);
                    }
                    onWheel: (wheel) => {
                        if (!toolbar.document)
                            return;
                        const step = wheel.angleDelta.y > 0 ? 1 : (wheel.angleDelta.y < 0 ? -1 : 0);
                        toolbar.document.strokeWidth = Math.max(strokeWidth.minimum,
                            Math.min(strokeWidth.maximum, Math.round(strokeWidth.value) + step));
                    }
                }
                ToolTip.visible: strokeMouse.containsMouse && !strokeMouse.pressed
                ToolTip.delay: 600
                ToolTip.text: qsTr("Stroke width")
            }
        }

        Group {
            IconButton {
                objectName: "undoButton"
                icon: "undo"
                tip: qsTr("Undo (Ctrl+Z)")
                enabled: toolbar.document !== null && toolbar.document.canUndo
                onClicked: toolbar.document.undo()
            }
            IconButton {
                objectName: "redoButton"
                icon: "redo"
                tip: qsTr("Redo (Ctrl+Shift+Z)")
                enabled: toolbar.document !== null && toolbar.document.canRedo
                onClicked: toolbar.document.redo()
            }
        }

        Group {
            IconButton {
                objectName: "copyButton"
                icon: "copy"
                tip: qsTr("Copy (Ctrl+C)")
                onClicked: {
                    if (toolbar.document)
                        toolbar.copied(toolbar.document.copyToClipboard());
                }
            }
            IconButton {
                objectName: "saveButton"
                icon: "save"
                tip: qsTr("Save (Ctrl+S)")
                onClicked: {
                    if (toolbar.document)
                        toolbar.document.quickSave("png");
                }
            }
            IconButton {
                objectName: "saveAsButton"
                icon: "saveas"
                tip: qsTr("Save as (Ctrl+Shift+S)")
                onClicked: toolbar.saveAsRequested()
            }
        }

        Group {
            IconButton {
                objectName: "zoomOutButton"
                icon: "zoomout"
                tip: qsTr("Zoom out (Ctrl+-)")
                onClicked: {
                    if (toolbar.canvas)
                        toolbar.canvas.zoomOut();
                }
            }
            Text {
                width: 46
                height: 34
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: toolbar.canvas ? Math.round(toolbar.canvas.zoom * 100) + "%" : ""
                color: toolbar.subtextColour
                font.family: toolbar.fontFamily
                font.pixelSize: 12
            }
            IconButton {
                objectName: "zoomInButton"
                icon: "zoomin"
                tip: qsTr("Zoom in (Ctrl+=)")
                onClicked: {
                    if (toolbar.canvas)
                        toolbar.canvas.zoomIn();
                }
            }
            IconButton {
                objectName: "zoomFitButton"
                icon: "fit"
                tip: qsTr("Fit to window (Ctrl+0)")
                checked: toolbar.canvas !== null && toolbar.canvas.fit
                onClicked: {
                    if (toolbar.canvas)
                        toolbar.canvas.fit = true;
                }
            }
        }
    }
}
