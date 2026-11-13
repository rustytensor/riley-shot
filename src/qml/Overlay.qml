// SPDX-License-Identifier: GPL-3.0-or-later
// One output's capture overlay. It shows that output's frozen grab device pixel for device pixel (the Image is the
// output's logical size; at the surface's scale that is exactly the grab's device size), and draws the region, the
// highlights and the capture bar over it. Every pointer position goes to the shared controller in layout
// coordinates (layoutX/layoutY + the local position); every key goes to controller.keyPressed.
//
// With no region and nothing highlighted, nothing but the frozen image is drawn outside the capture bar.
import QtQuick

Item {
    id: root

    required property CaptureController controller
    required property string outputName
    required property int layoutX
    required property int layoutY
    required property int deviceWidth
    required property int deviceHeight
    required property real outputScale

    readonly property var theme: controller.theme
    readonly property color accentColour: theme.accent !== undefined ? theme.accent : "#e9a046"
    readonly property color textColour: theme.text !== undefined ? theme.text : "#f0ece0"
    readonly property color baseColour: theme.base !== undefined ? theme.base : "#151a23"
    readonly property string fontFamily: theme.font !== undefined ? theme.font : "IBM Plex Mono"

    width: deviceWidth / outputScale
    height: deviceHeight / outputScale
    focus: true

    Keys.onPressed: (event) => {
        event.accepted = root.controller.keyPressed(event.key, event.modifiers);
    }

    // A layout rect in this overlay's units, and whether it overlaps the overlay.
    function local(r) {
        return Qt.rect(r.x - layoutX, r.y - layoutY, r.width, r.height);
    }
    function overlaps(r) {
        return r.width > 0 && r.height > 0 && r.x < width && r.x + r.width > 0 && r.y < height
                && r.y + r.height > 0;
    }

    readonly property rect localSelection: local(controller.selection)
    readonly property bool selectionHere: controller.hasSelection && overlaps(localSelection)
    readonly property rect localWindow: local(controller.hoverWindow)
    readonly property bool regionMode: controller.mode === CaptureController.Region

    Image {
        objectName: "frozenImage"
        x: 0
        y: 0
        width: root.deviceWidth / root.outputScale
        height: root.deviceHeight / root.outputScale
        source: "image://frozen/" + root.outputName
        smooth: false
        mipmap: false
        cache: false
        asynchronous: false
    }

    // Dimming around the region, only while there is one (Region mode). The four bands are clamped to the overlay,
    // so a region on another output dims all of this one.
    Item {
        id: dim
        anchors.fill: parent
        visible: root.regionMode && root.controller.hasSelection
        readonly property color shade: Qt.rgba(0, 0, 0, 0.45)
        readonly property real x0: Math.max(0, Math.min(root.width, root.localSelection.x))
        readonly property real x1: Math.max(0, Math.min(root.width, root.localSelection.x + root.localSelection.width))
        readonly property real y0: Math.max(0, Math.min(root.height, root.localSelection.y))
        readonly property real y1: Math.max(0, Math.min(root.height, root.localSelection.y + root.localSelection.height))

        Rectangle { x: 0; y: 0; width: root.width; height: dim.y0; color: dim.shade }
        Rectangle { x: 0; y: dim.y1; width: root.width; height: root.height - dim.y1; color: dim.shade }
        Rectangle { x: 0; y: dim.y0; width: dim.x0; height: dim.y1 - dim.y0; color: dim.shade }
        Rectangle { x: dim.x1; y: dim.y0; width: root.width - dim.x1; height: dim.y1 - dim.y0; color: dim.shade }
    }

    // Screen mode: the whole output under the pointer.
    Rectangle {
        objectName: "screenHighlight"
        anchors.fill: parent
        visible: root.controller.mode === CaptureController.Screen && root.controller.hoverOutput === root.outputName
        color: Qt.alpha(root.accentColour, 0.12)
        border.width: 4
        border.color: root.accentColour
    }

    // Window mode: the window under the pointer.
    Rectangle {
        objectName: "windowHighlight"
        x: root.localWindow.x
        y: root.localWindow.y
        width: root.localWindow.width
        height: root.localWindow.height
        visible: root.controller.mode === CaptureController.Window && root.overlaps(root.localWindow)
        color: Qt.alpha(root.accentColour, 0.18)
        border.width: 3
        border.color: root.accentColour
    }

    // The pointer: every position, with and without a button, in layout coordinates.
    MouseArea {
        id: pointerArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        property int handle: 0

        function updateHandle(mouse) {
            handle = root.controller.handleAt(root.layoutX + mouse.x, root.layoutY + mouse.y);
        }

        cursorShape: {
            if (!root.regionMode)
                return Qt.PointingHandCursor;
            switch (handle) {
            case 1: return Qt.SizeAllCursor;   // Move
            case 2: case 6: return Qt.SizeFDiagCursor; // TopLeft, BottomRight
            case 4: case 8: return Qt.SizeBDiagCursor; // TopRight, BottomLeft
            case 3: case 7: return Qt.SizeVerCursor;   // Top, Bottom
            case 5: case 9: return Qt.SizeHorCursor;   // Right, Left
            default: return Qt.CrossCursor;
            }
        }

        onPressed: (mouse) => {
            root.forceActiveFocus();
            root.controller.pointerPressed(root.layoutX + mouse.x, root.layoutY + mouse.y, mouse.modifiers);
        }
        onPositionChanged: (mouse) => {
            root.controller.pointerMoved(root.layoutX + mouse.x, root.layoutY + mouse.y, mouse.modifiers);
            if (!pressed)
                updateHandle(mouse);
        }
        onReleased: (mouse) => {
            root.controller.pointerReleased(root.layoutX + mouse.x, root.layoutY + mouse.y, mouse.modifiers);
            updateHandle(mouse);
        }
    }

    SelectionFrame {
        objectName: "selectionFrame"
        x: root.localSelection.x
        y: root.localSelection.y
        width: root.localSelection.width
        height: root.localSelection.height
        visible: root.selectionHere
        colour: root.accentColour
        handleFill: root.textColour
    }

    // The region's size in device pixels, in a pill under the region (above it, or inside it, when there is no
    // room), kept inside the overlay.
    Rectangle {
        id: sizePill
        visible: root.selectionHere
        width: sizeLabel.implicitWidth + 16
        height: sizeLabel.implicitHeight + 8
        radius: height / 2
        color: Qt.alpha(root.baseColour, 0.85)
        readonly property real gap: 10
        x: Math.max(4, Math.min(root.width - width - 4,
                                root.localSelection.x + (root.localSelection.width - width) / 2))
        y: {
            const below = root.localSelection.y + root.localSelection.height + gap;
            if (below + height <= root.height - 4)
                return below;
            const above = root.localSelection.y - gap - height;
            if (above >= 4)
                return above;
            return Math.max(4, Math.min(root.height - height - 4,
                                        root.localSelection.y + root.localSelection.height - height - gap));
        }

        Text {
            id: sizeLabel
            objectName: "sizeLabel"
            anchors.centerIn: parent
            visible: root.selectionHere
            text: root.controller.sizeText
            color: root.textColour
            font.family: root.fontFamily
            font.pixelSize: 13
        }
    }

    // The capture bar: on the output under the pointer only, centred, 48 px above the bottom edge.
    CaptureBar {
        objectName: "captureBar"
        controller: root.controller
        visible: root.controller.barVisible && root.controller.barOutput === root.outputName
        x: Math.round((root.width - width) / 2)
        y: Math.round(root.height - height - 48)
    }
}
