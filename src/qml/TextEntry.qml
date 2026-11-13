// SPDX-License-Identifier: GPL-3.0-or-later
// The Text tool's input: a one-line field shown over the canvas where the Text tool was pressed, in the text
// colour, font and size the text will have (times the canvas zoom). Enter or Return places the text through
// document.placeText(); Escape drops it. Editor.qml positions it and calls openAt().
pragma ComponentBehavior: Bound

import QtQuick

FocusScope {
    id: entry

    property var document: null
    // The canvas zoom, so the field previews the text at the size it will be drawn.
    property real zoom: 1
    property var theme: ({})

    // Where the text goes, in image coordinates.
    property point imagePoint: Qt.point(0, 0)
    readonly property alias text: field.text

    // Emitted after the field hides, whether the text was placed or dropped.
    signal finished()

    // The field opens with the Text tool current, so the current colour is the Text tool's.
    readonly property color inkColour: document ? document.colour : "#e5776a"
    readonly property real pixelSize: Math.max(6, (document ? document.textSize : 24) * zoom)

    visible: false
    width: Math.max(field.contentWidth, pixelSize * 2) + 8
    height: field.implicitHeight + 4

    function openAt(point) {
        if (visible)
            commit();
        imagePoint = point;
        field.text = "";
        visible = true;
        forceActiveFocus();
        field.forceActiveFocus();
    }

    // Places the text (when there is any) and hides the field.
    function commit() {
        if (!visible)
            return;
        const t = field.text;
        visible = false;
        if (document && t.length > 0)
            document.placeText(imagePoint, t);
        field.text = "";
        finished();
    }

    function cancel() {
        if (!visible)
            return;
        visible = false;
        field.text = "";
        finished();
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -2
        color: Qt.rgba(0, 0, 0, 0.08)
        border.width: 1
        border.color: entry.theme.accent !== undefined ? entry.theme.accent : "#e9a046"
        radius: 3
    }

    TextInput {
        id: field
        x: 4
        y: 2
        width: Math.max(contentWidth, entry.pixelSize)
        focus: true
        color: entry.inkColour
        selectionColor: entry.theme.accent !== undefined ? entry.theme.accent : "#e9a046"
        font.family: entry.document ? entry.document.fontFamily : "IBM Plex Mono"
        font.pixelSize: entry.pixelSize
        Keys.onReturnPressed: (event) => { event.accepted = true; entry.commit(); }
        Keys.onEnterPressed: (event) => { event.accepted = true; entry.commit(); }
        Keys.onEscapePressed: (event) => { event.accepted = true; entry.cancel(); }
    }
}
