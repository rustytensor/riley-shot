// SPDX-License-Identifier: GPL-3.0-or-later
// The editor's window content (Windows 11 Snipping Tool style): the toolbar on top, the shot under it fit to the
// window (zoomable, scrollable when larger), and the Text tool's input over the canvas. The contract (properties,
// objectNames, shortcuts) is written in src/app/editorwindow.h.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

Item {
    id: editor

    required property Document document
    required property var theme
    // The 8 swatch colours (paletteList()). It replaces Item's own palette, which nothing here uses.
    override required property var palette

    readonly property color baseColour: theme.base !== undefined ? theme.base : "#151a23"
    readonly property color surfaceColour: theme.surface0 !== undefined ? theme.surface0 : "#1d2430"
    readonly property color edgeColour: theme.surface1 !== undefined ? theme.surface1 : "#283244"
    readonly property color textColour: theme.text !== undefined ? theme.text : "#f0ece0"
    readonly property color accentColour: theme.accent !== undefined ? theme.accent : "#e9a046"
    readonly property color errorColour: theme.red !== undefined ? theme.red : "#e5776a"
    readonly property string fontFamily: theme.font !== undefined ? theme.font : "IBM Plex Mono"

    focus: true

    // Shortcuts are off while the text input has the keyboard.
    readonly property bool shortcutsOn: !textInput.activeFocus

    function copy() {
        const ok = document.copyToClipboard();
        notice.show(ok ? qsTr("Copied to the clipboard") : document.lastError, !ok);
    }
    function save() {
        document.quickSave("png");
    }
    function saveAs() {
        saveDialog.open();
    }

    Rectangle {
        anchors.fill: parent
        color: editor.baseColour
    }

    EditorToolbar {
        id: toolbar
        objectName: "toolbar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        document: editor.document
        canvas: canvas
        theme: editor.theme
        colours: editor.palette
        onSaveAsRequested: editor.saveAs()
        onCopied: (ok) => notice.show(ok ? qsTr("Copied to the clipboard") : editor.document.lastError, !ok)
    }

    Item {
        id: viewport
        objectName: "viewport"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: toolbar.bottom
        anchors.bottom: parent.bottom
        clip: true

        Flickable {
            id: flick
            anchors.fill: parent
            // Dragging belongs to the tools; scrolling is the wheel, the scroll bars and the middle button.
            interactive: false
            contentWidth: Math.max(width, canvas.width)
            contentHeight: Math.max(height, canvas.height)
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.horizontal: ScrollBar { policy: canvas.width > flick.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
            ScrollBar.vertical: ScrollBar { policy: canvas.height > flick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }

            function scrollTo(x, y) {
                contentX = Math.max(0, Math.min(contentWidth - width, x));
                contentY = Math.max(0, Math.min(contentHeight - height, y));
            }

            DocumentCanvas {
                id: canvas
                objectName: "canvas"
                document: editor.document
                accent: editor.accentColour
                availableWidth: viewport.width
                availableHeight: viewport.height
                width: implicitWidth
                height: implicitHeight
                x: Math.max(0, (flick.width - width) / 2)
                y: Math.max(0, (flick.height - height) / 2)
            }

            // The Text tool's field, over the canvas at the point the tool was pressed. The field's text starts 4 px
            // right and 2 px down from its corner (TextEntry.qml), so the corner sits that much up and left.
            TextEntry {
                id: textInput
                objectName: "textInput"
                document: editor.document
                theme: editor.theme
                zoom: canvas.zoom
                readonly property point crop: editor.document ? Qt.point(editor.document.cropRect.x,
                                                                          editor.document.cropRect.y)
                                                              : Qt.point(0, 0)
                x: canvas.x + (imagePoint.x - crop.x) * canvas.zoom - 4
                y: canvas.y + (imagePoint.y - crop.y) * canvas.zoom - 2
                onFinished: editor.forceActiveFocus()
            }
        }

        // Ctrl+wheel zooms; the wheel scrolls (Shift+wheel sideways).
        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: (event) => {
                if (event.angleDelta.y > 0)
                    canvas.zoomIn();
                else if (event.angleDelta.y < 0)
                    canvas.zoomOut();
            }
        }
        WheelHandler {
            acceptedModifiers: Qt.NoModifier
            onWheel: (event) => flick.scrollTo(flick.contentX - event.angleDelta.x, flick.contentY - event.angleDelta.y)
        }
        WheelHandler {
            acceptedModifiers: Qt.ShiftModifier
            onWheel: (event) => flick.scrollTo(flick.contentX - event.angleDelta.y, flick.contentY)
        }

        // Middle-button drag pans. Left presses pass through to the canvas.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.MiddleButton
            property point last
            onPressed: (mouse) => last = Qt.point(mouse.x, mouse.y)
            onPositionChanged: (mouse) => {
                flick.scrollTo(flick.contentX - (mouse.x - last.x), flick.contentY - (mouse.y - last.y));
                last = Qt.point(mouse.x, mouse.y);
            }
        }

        // A short line after a save, a copy or a failure.
        Rectangle {
            id: notice
            property bool failed: false
            property alias text: noticeText.text
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 16
            width: Math.min(parent.width - 32, noticeText.implicitWidth + 28)
            height: noticeText.implicitHeight + 16
            radius: height / 2
            color: editor.surfaceColour
            border.width: 1
            border.color: failed ? editor.errorColour : editor.edgeColour
            visible: false

            function show(line, isError) {
                if (!line)
                    return;
                text = line;
                failed = isError;
                visible = true;
                noticeTimer.restart();
            }

            Text {
                id: noticeText
                anchors.centerIn: parent
                width: Math.min(implicitWidth, notice.parent.width - 60)
                elide: Text.ElideMiddle
                color: notice.failed ? editor.errorColour : editor.textColour
                font.family: editor.fontFamily
                font.pixelSize: 13
            }
            Timer {
                id: noticeTimer
                interval: 2500
                onTriggered: notice.visible = false
            }
        }
    }

    Connections {
        target: editor.document
        function onTextRequested(point) {
            textInput.openAt(point);
        }
        function onSaved(path) {
            notice.show(qsTr("Saved %1").arg(path), false);
        }
        function onLastErrorChanged() {
            if (editor.document.lastError !== "")
                notice.show(editor.document.lastError, true);
        }
        function onToolChanged() {
            if (editor.document.tool !== Document.Text)
                textInput.commit();
        }
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Save as")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: [qsTr("PNG image (*.png)"), qsTr("JPEG image (*.jpg *.jpeg)")]
        currentFolder: editor.document && editor.document.saveFolder !== ""
                       ? "file://" + editor.document.saveFolder : ""
        onAccepted: editor.document.saveAs(selectedFile.toString())
    }

    Shortcut { sequences: ["Ctrl+Z"]; enabled: editor.shortcutsOn; onActivated: editor.document.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; enabled: editor.shortcutsOn; onActivated: editor.document.redo() }
    Shortcut { sequences: ["Ctrl+C"]; enabled: editor.shortcutsOn; onActivated: editor.copy() }
    Shortcut { sequences: ["Ctrl+S"]; enabled: editor.shortcutsOn; onActivated: editor.save() }
    Shortcut { sequences: ["Ctrl+Shift+S"]; enabled: editor.shortcutsOn; onActivated: editor.saveAs() }
    Shortcut { sequences: ["Delete", "Backspace"]; enabled: editor.shortcutsOn; onActivated: editor.document.removeSelected() }
    Shortcut { sequences: ["Escape"]; enabled: editor.shortcutsOn; onActivated: editor.document.selectedId = -1 }
    Shortcut { sequences: ["Ctrl+=", "Ctrl++"]; enabled: editor.shortcutsOn; onActivated: canvas.zoomIn() }
    Shortcut { sequences: ["Ctrl+-"]; enabled: editor.shortcutsOn; onActivated: canvas.zoomOut() }
    Shortcut { sequences: ["Ctrl+0"]; enabled: editor.shortcutsOn; onActivated: canvas.fit = true }
}
