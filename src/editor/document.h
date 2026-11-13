// SPDX-License-Identifier: GPL-3.0-or-later
// The editor's document: the shot, its annotations (annotation.h), the crop, the undo stack, and the renderer that
// makes the exact output image. The QML editor (src/qml/Editor.qml) shows render() through DocumentCanvas
// (src/app/documentcanvas.h) and drives the tools through press/move/release; tests drive the same API directly and
// check rendered pixels.
//
// Coordinates: everything here (points, rects, press/move/release) is in the ORIGINAL image's pixel coordinates,
// pixel (x,y) covering [x,x+1) x [y,y+1). The crop does not move annotations: render() pixel (x,y) shows image
// point (x + cropRect.x, y + cropRect.y).
//
// ---------------------------------------------------------------------------------------------------------------
// How render() draws (the M3 gate's hand-worked pixels depend on every line here):
//   - Output: QImage::Format_RGB32, size cropRect().size(): the base image's cropRect part, then every annotation
//     in z-order (ids() order, bottom first), then the object being dragged (if any) last. One QPainter with
//     QPainter::Antialiasing on (text also TextAntialiasing), translated by -cropRect.topLeft().
//     Nothing else: no selection outline, no handles, no cursor (DocumentCanvas draws those over it).
//   - Pen: QPen(colour, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin), the points as one polyline path.
//   - Highlighter: the same path in ONE QPainterPath stroked once, colour with its alpha set to 128 (whatever the
//     colour's own alpha), Qt::RoundCap, Qt::RoundJoin, CompositionMode_SourceOver. Where the path crosses itself
//     the colour is applied once, never twice (no darker spots).
//   - Line: QPen(colour, width, Qt::SolidLine, Qt::RoundCap) from points[0] to points[1].
//   - Arrow (from = points[0], to = points[1], w = width, length = |to - from|):
//       head length L = max(4*w, 12), head half-width H = max(2*w, 6); when length < L, then L = length.
//       The head is a filled triangle (colour, no outline) with its tip exactly at `to` and its base, of half-width
//       H, perpendicular to the arrow at distance L from `to`. The shaft is QPen(colour, w, Qt::SolidLine,
//       Qt::FlatCap) from `from` to the point L/2 back from `to` (inside the head). Nothing is drawn at `from`
//       beyond the shaft's flat end, and nothing beyond the tip.
//   - Rectangle: QPen(colour, width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin) centred on the rect's edges
//     (square corners). Filled: the same outline, and the rect's inside filled with colour.
//   - Ellipse: QPen(colour, width) centred on the ellipse inscribed in rect. Filled: the inside filled with colour,
//     and the same outline.
//   - Text: QFont(fontFamily) with setPixelSize(qRound(textSize)), colour; each line of `text` (split on '\n') drawn
//     with its baseline at position.y + ascent + i * lineSpacing (QFontMetricsF), left edge at position.x.
//     Its box (annotationBounds) is QRectF(position, QSizeF(widest line's horizontalAdvance,
//     fm.height() + (lines - 1) * fm.lineSpacing())).
//   - Marker: a filled circle of radius 14 (no outline) in colour, centred on position; on it the number as text,
//     QFont(fontFamily) bold with setPixelSize(16), in markerTextColour (default #ffffff), centred: horizontally on
//     position.x by its horizontalAdvance, baseline at position.y + (ascent - descent) / 2.
//   - Blur: the pixels drawn so far (base and every annotation below it) inside rect are replaced by a blur of
//     them that samples only pixels inside rect (clamped at its edges) and mixes every pixel with pixels at least
//     8 px away in each direction (for example a box blur of radius >= 8, or a Gaussian of sigma >= 4). Strong
//     enough that on a black and white 8x8 checkerboard every pixel at least 4 px inside rect ends up with each
//     channel within [48, 207]. Annotations above it are drawn sharp over the result.
//   - Pixelate: the pixels drawn so far inside rect are cut into 10x10 blocks from rect's top-left (blocks at the
//     right and bottom edges may be smaller); every pixel of a block becomes the block's mean, per channel,
//     rounded to nearest: (sum + n/2) / n in integers, n the block's pixel count.
//   Blur and Pixelate rects are snapped to whole pixels (QRectF::toAlignedRect is NOT used: the rect's x, y,
//   width, height are each rounded with qRound) and clipped to the image.
//
// Hit testing (annotationAt, the eraser and the Select tool): a point hits
//   - Pen, Highlighter, Line, Arrow: when within width/2 + 3 of the drawn path (segments, distance to the nearest
//     point of any segment);
//   - Rectangle, Ellipse (outline or filled), Blur, Pixelate, Text, Marker: when inside annotationBounds grown by
//     width/2 + 3 on every side (Text, Marker, Blur, Pixelate: grown by 3).
//   The topmost annotation hit wins; -1 when none.
//
// annotationBounds(id):
//   Pen, Highlighter, Line, Arrow: the bounding rect of the points (stroke width not included).
//   Rectangle, Ellipse, Blur, Pixelate: rect.   Text: the text box above.
//   Marker: QRectF(position.x - 14, position.y - 14, 28, 28).
//
// Undo: every change below that says "one undo step" is one entry; undo() puts the document back exactly as it was
// before it (pixels and ids), redo() exactly as after. A new step after an undo drops everything that could be
// redone. setImage() clears the stack. Saving and copying never touch the document or the stack.
//
// Owner: builder A (document.cpp; any helpers in src/editor/render.h/.cpp). The declarations below are the M3
// contract tests/tst_document.cpp and tests/tst_editor_qml.cpp drive; do not change them.
#pragma once

#include "annotation.h"

#include <QColor>
#include <QDateTime>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace rileyshot {

class Document : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("the editor makes its document")

    // The base image's full size (not cropped).
    Q_PROPERTY(QSize imageSize READ imageSize NOTIFY imageChanged)
    // The part of the image render() gives, in image coordinates; the whole image when not cropped.
    Q_PROPERTY(QRect cropRect READ cropRect NOTIFY cropChanged)
    // cropRect().size(): the size of render(), of the saved file and of the copied image.
    Q_PROPERTY(QSize outputSize READ outputSize NOTIFY cropChanged)
    // The current tool (Tool). Changing it ends any drag (as release at the last point) and clears the selection
    // unless the new tool is Select.
    Q_PROPERTY(int tool READ tool WRITE setTool NOTIFY toolChanged)
    // The current tool's colour (see toolColour): setting it sets toolColour(tool).
    Q_PROPERTY(QColor colour READ colour WRITE setColour NOTIFY colourChanged)
    // Stroke width in image pixels for new Pen, Highlighter, Line, Arrow, Rectangle and Ellipse objects;
    // default 4; values below 1 become 1.
    Q_PROPERTY(qreal strokeWidth READ strokeWidth WRITE setStrokeWidth NOTIFY strokeWidthChanged)
    // New rectangles and ellipses are filled; default false.
    Q_PROPERTY(bool filled READ filled WRITE setFilled NOTIFY filledChanged)
    // Pixel size for placeText(); default 24.
    Q_PROPERTY(qreal textSize READ textSize WRITE setTextSize NOTIFY textSizeChanged)
    // Font family for Text and Marker objects; default "IBM Plex Mono" (Theme::font's default).
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontFamilyChanged)
    // The selected annotation's id (Select tool), -1 for none. Setting an id that does not exist sets -1.
    Q_PROPERTY(int selectedId READ selectedId WRITE setSelectedId NOTIFY selectionChanged)
    // annotationBounds(selectedId), QRectF() for none; also notified when the selected object moves or resizes.
    Q_PROPERTY(QRectF selectedBounds READ selectedBounds NOTIFY selectionChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    Q_PROPERTY(int annotationCount READ annotationCount NOTIFY changed)
    // One line about the last failed save or copy; "" after a success.
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    // The path the last successful save wrote; "" before.
    Q_PROPERTY(QString lastSavedPath READ lastSavedPath NOTIFY saved)

public:
    enum Tool {
        Select = 0, // press on an object selects it; drag moves it; drag one of its 8 handles resizes it
        Pen,
        Highlighter,
        Eraser,     // press, and every move while pressed, removes the topmost object hit
        Line,
        Arrow,
        Rectangle,
        Ellipse,
        Text,       // press emits textRequested(point); QML then calls placeText()
        Marker,     // press adds a numbered marker centred on the point
        Crop,       // drag a rect; release crops to it
        Blur,       // drag a rect; release adds a Blur object
        Pixelate    // drag a rect; release adds a Pixelate object
    };
    Q_ENUM(Tool)

    explicit Document(QObject *parent = nullptr);
    ~Document() override;

    // --- the shot ------------------------------------------------------------------------------------------------
    // Starts over on image (converted to Format_RGB32): no annotations, no crop, empty undo stack, nothing
    // selected, the next annotation id 1. Tool, colours, widths, fonts and the save/copy settings stay.
    void setImage(const QImage &image);
    QImage image() const; // the base image, uncropped, without annotations
    QSize imageSize() const;
    QRect cropRect() const;
    QSize outputSize() const;

    // The exact output image (see "How render() draws"). Cached: recomputed only after a change.
    QImage render() const;

    // --- tool settings -------------------------------------------------------------------------------------------
    int tool() const;
    void setTool(int tool); // values outside Select..Pixelate are ignored
    QColor colour() const;
    void setColour(const QColor &colour);
    // Each tool keeps its own colour. Defaults (Theme() and Config() defaults): Highlighter #e3c169 (yellow),
    // Marker #e9a046 (accent), every other tool #e5776a (red).
    QColor toolColour(int tool) const;
    void setToolColour(int tool, const QColor &colour);
    qreal strokeWidth() const;
    void setStrokeWidth(qreal width);
    bool filled() const;
    void setFilled(bool on);
    qreal textSize() const;
    void setTextSize(qreal size);
    QString fontFamily() const;
    void setFontFamily(const QString &family);
    QColor markerTextColour() const;
    void setMarkerTextColour(const QColor &colour);

    // --- adding objects: each is one undo step and returns the new id (-1 when nothing was added) --------------
    // Pen or Highlighter (kind), at least one point.
    int addStroke(Annotation::Kind kind, const QList<QPointF> &points, const QColor &colour, qreal width);
    // Nothing is added when from == to.
    int addLine(const QPointF &from, const QPointF &to, const QColor &colour, qreal width);
    int addArrow(const QPointF &from, const QPointF &to, const QColor &colour, qreal width);
    // rect is normalised first; nothing is added when it has no area.
    int addRectangle(const QRectF &rect, const QColor &colour, qreal width, bool filled);
    int addEllipse(const QRectF &rect, const QColor &colour, qreal width, bool filled);
    // Font: fontFamily(). Nothing is added for empty text.
    int addText(const QPointF &topLeft, const QString &text, const QColor &colour, qreal pixelSize);
    // number = 1 + the highest number among the markers now in the document (1 when there are none).
    int addMarker(const QPointF &centre, const QColor &colour);
    // Nothing is added when the rect, snapped and clipped to the image, is empty.
    int addBlur(const QRectF &rect);
    int addPixelate(const QRectF &rect);
    // The Text tool's second half: addText(topLeft, text, colour of the Text tool, textSize()).
    Q_INVOKABLE int placeText(const QPointF &topLeft, const QString &text);

    // --- reading objects -----------------------------------------------------------------------------------------
    int annotationCount() const;
    QList<int> ids() const;                 // z-order, bottom first
    Annotation annotation(int id) const;    // id -1 when there is no such object
    Q_INVOKABLE QRectF annotationBounds(int id) const; // QRectF() when there is no such object
    Q_INVOKABLE int annotationAt(const QPointF &point) const;

    // --- changing objects: each is one undo step; false (and no step) when id does not exist or nothing changes --
    // Moves every point, rect and position by delta.
    bool moveAnnotation(int id, const QPointF &delta);
    // Rectangle, Ellipse, Blur, Pixelate: rect = bounds (normalised). Pen, Highlighter, Line, Arrow: every point is
    // mapped from the old bounds to the new: x' = new.x + (x - old.x) * new.w / old.w (when old.w is 0:
    // x' = x + new.x - old.x), the same for y. Text and Marker cannot be resized: false.
    bool setAnnotationRect(int id, const QRectF &bounds);
    bool removeAnnotation(int id);
    // Removes the topmost object at point (annotationAt). False when there is none.
    bool erase(const QPointF &point);
    // cropRect = rect intersected with the whole image (the original, not the current crop). False and nothing
    // changes when that is empty. One undo step.
    bool crop(const QRect &rect);

    // --- the tools, as the canvas drives them (image coordinates; modifiers are Qt::KeyboardModifiers as an int,
    // reserved) --------------------------------------------------------------------------------------------------
    //   Pen, Highlighter: press starts a stroke at the point, each move adds a point, release adds the last point
    //     and makes it one object (one undo step) with the tool's colour and strokeWidth.
    //   Line, Arrow: from the press point to the release point (nothing when they are equal).
    //   Rectangle, Ellipse: the normalised rect between press and release points, filled per `filled` (nothing
    //     when it has no area).
    //   Blur, Pixelate: the same rect, as addBlur/addPixelate.   Crop: crop() to that rect (toAlignedRect()).
    //   Marker: press adds addMarker(point, tool colour); move and release do nothing.
    //   Text: press emits textRequested(point); nothing else.
    //   Eraser: press and every move while pressed erase(point); everything erased in one press-release is ONE
    //     undo step.
    //   Select: press within 6 px (Euclidean) of one of the selected object's 8 handles (corners and edge
    //     midpoints of selectedBounds) starts a resize, unless it is Text or Marker; otherwise press selects
    //     annotationAt(point) (-1 deselects) and starts moving it. Moves follow the pointer; release makes the
    //     whole drag one undo step (none when it ended where it started). Dragging a corner handle moves that
    //     corner, an edge handle that edge; the opposite side stays.
    //   While a drag is under way render() shows the object being made or moved; it is not on the undo stack
    //   until release.
    Q_INVOKABLE void press(const QPointF &point, int modifiers = 0);
    Q_INVOKABLE void move(const QPointF &point, int modifiers = 0);
    Q_INVOKABLE void release(const QPointF &point, int modifiers = 0);
    int selectedId() const;
    void setSelectedId(int id);
    QRectF selectedBounds() const;
    // removeAnnotation(selectedId()). The Delete key.
    Q_INVOKABLE bool removeSelected();

    // --- undo ----------------------------------------------------------------------------------------------------
    bool canUndo() const;
    bool canRedo() const;
    Q_INVOKABLE void undo(); // does nothing when !canUndo()
    Q_INVOKABLE void redo(); // does nothing when !canRedo()

    // --- save and copy: always render(), never changing the document ----------------------------------------------
    // The copy program and arguments (deliver.h copyImage); default copyCommandFromEnvironment().
    QStringList copyCommand() const;
    void setCopyCommand(const QStringList &command);
    // Where quickSave() writes (already resolved, no "~"), and its file name pattern (deliver.h fileNameFor).
    QString saveFolder() const;
    void setSaveFolder(const QString &folder);
    QString fileNamePattern() const;
    void setFileNamePattern(const QString &pattern);
    // The time used for quickSave()'s file name; invalid (the default) means the time of the call.
    void setNow(const QDateTime &now);

    // copyImage(render(), copyCommand()): image/png bytes to the clipboard program. Ctrl+C.
    Q_INVOKABLE bool copyToClipboard();
    // saveImage(render(), saveFolder, fileNamePattern, now, format): format "png" (default) or "jpg"/"jpeg";
    // never overwrites. Returns the path written, "" on failure (lastError set). Ctrl+S.
    Q_INVOKABLE QString quickSave(const QString &format = QStringLiteral("png"));
    // Writes render() to exactly this path (a "file://" URL is turned into its local path), overwriting:
    // JPEG (quality 92) when the name ends in .jpg or .jpeg (any case), PNG otherwise. Save as.
    Q_INVOKABLE bool saveAs(const QString &pathOrUrl);
    QString lastError() const;
    QString lastSavedPath() const;

signals:
    void imageChanged();
    void cropChanged();
    void toolChanged();
    void colourChanged();
    void strokeWidthChanged();
    void filledChanged();
    void textSizeChanged();
    void fontFamilyChanged();
    void selectionChanged();
    void undoStateChanged();
    void lastErrorChanged();
    // Emitted after anything that changes render() (the canvas repaints on it).
    void changed();
    // The Text tool was pressed at point (image coordinates).
    void textRequested(QPointF point);
    void saved(const QString &path);

private:
    struct Private;
    Private *d;
};

} // namespace rileyshot
