// SPDX-License-Identifier: GPL-3.0-or-later
// The editor window (Windows 11 Snipping Tool style): a normal window (not a layer surface) titled "riley-shot"
// whose content is RileyShot/Editor.qml over one Document.
//
// Editor.qml (RileyShot module, loaded with QQmlComponent::loadFromModule("RileyShot", "Editor")): root Item; the
// window (or the test) sets its width and height and everything lays out inside. Required properties:
//     document (Document), theme (var: themeMap()), palette (var: paletteList(), 8 colours).
//   In the Riley theme (theme colours and font). Children by objectName:
//     "toolbar"          the top toolbar, holding everything below except the viewport.
//     "tool_select", "tool_pen", "tool_highlighter", "tool_eraser", "tool_line", "tool_arrow", "tool_rectangle",
//     "tool_ellipse", "tool_text", "tool_marker", "tool_crop", "tool_blur", "tool_pixelate"
//                        buttons; a click sets document.tool to Document.Select ... Document.Pixelate; each has a
//                        bool property `checked` == (document.tool is its tool).
//     "fillToggle"       `checked` == document.filled; a click toggles document.filled.
//     "swatch0" ... "swatch7"  a click sets document.colour = palette[i].
//     "customColour"     opens a colour dialog that sets document.colour.
//     "strokeWidth"      a control whose `value` follows document.strokeWidth and sets it (1 to 32).
//     "undoButton", "redoButton"   `enabled` == document.canUndo / canRedo; a click undoes / redoes.
//     "copyButton"       document.copyToClipboard().
//     "saveButton"       document.quickSave("png").
//     "saveAsButton"     a file dialog, then document.saveAs(chosen file) (".jpg"/".jpeg" names give JPEG).
//     "zoomInButton", "zoomOutButton", "zoomFitButton"   canvas.zoomIn(), canvas.zoomOut(), canvas.fit = true.
//     "viewport"         the area under the toolbar that shows the canvas (scrollable when the canvas is larger),
//                        canvas centred in it when smaller.
//     "canvas"           a DocumentCanvas (documentcanvas.h) inside the viewport; availableWidth/availableHeight ==
//                        viewport width/height; width/height == its implicit size.
//     "textInput"        a text field, visible only after document.textRequested(point): placed over the canvas at
//                        that point, given active focus; Enter or Return calls document.placeText(point, its text)
//                        and hides it; Escape hides it without adding anything.
//   Shortcuts (whole window, not while "textInput" has focus): Ctrl+Z undo, Ctrl+Shift+Z and Ctrl+Y redo,
//   Ctrl+C copy, Ctrl+S quickSave("png"), Ctrl+Shift+S save as, Delete and Backspace removeSelected(), Escape
//   clears the selection, Ctrl+= / Ctrl++ zoom in, Ctrl+- zoom out, Ctrl+0 fit.
//
// Owner: builder B (editorwindow.cpp, documentcanvas.*, src/qml/Editor.qml, EditorToolbar.qml, ColourPicker.qml,
// TextEntry.qml; and the wiring in session.cpp, main.cpp and cli.cpp). The declarations below are the M3 contract
// tests/tst_editor_qml.cpp drives; do not change them.
#pragma once

#include "cli.h"
#include "config.h"
#include "theme.h"

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace rileyshot {

class Document;

struct EditorOptions
{
    Config config;
    Theme theme;
    QStringList copyCommand; // empty: copyCommandFromEnvironment()
    QString home;            // for Config::resolvedSaveFolder; empty: QDir::homePath()
    int exitAfterMs = 0;     // > 0: the window closes by itself after this many ms, and runEditor returns 3
    QString reportFile;      // JSON lines, as session.h: {"event":"editor","width":W,"height":H} once after the
                             // window's first frame (W, H: document.imageSize), {"event":"exit","code":N} at the end
};

// Prepares a document for the editor: setImage(image); tool colours from config.colours through theme.resolve()
// (Pen: pen; Highlighter: highlighter; Line, Arrow, Rectangle, Ellipse: shape; Text: text; Marker: marker; a colour
// that does not resolve keeps the document's default); fontFamily = theme.font; saveFolder =
// config.resolvedSaveFolder(home); fileNamePattern = config.fileNamePattern; copyCommand as above.
void setUpDocument(Document *document, const QImage &image, const EditorOptions &options);

// The theme for QML, the same map CaptureController::theme gives: keys base surface0 surface1 text subtext accent
// mauve lavender teal peach green red yellow (QColor) and font (QString).
QVariantMap themeMap(const Theme &theme);
// Theme::palette() as QColor values, for Editor.qml's palette.
QVariantList paletteList(const Theme &theme);

// Shows the editor on image in an existing QGuiApplication and runs until its window is closed (0) or exitAfterMs
// runs out (3).
int runEditor(const QImage &image, const EditorOptions &options);

// `riley-shot edit FILE`: Config::load(Config::defaultPath()) (a broken file: its error on stderr, then the
// defaults), Theme::load(Theme::defaultPath()), the image from options.inFile; then runEditor with
// options.exitAfterMs and options.reportFile. Returns 4 (after one line on stderr, and the report's exit line)
// when the file cannot be read as an image.
int runEditFile(const CliOptions &options);

} // namespace rileyshot
