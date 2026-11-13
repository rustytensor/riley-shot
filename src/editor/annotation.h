// SPDX-License-Identifier: GPL-3.0-or-later
// One object drawn on a shot in the editor. Annotations stay objects (movable, resizable, deletable) until the
// document is rendered for saving or copying; render() bakes them into pixels, the document itself never does.
//
// Coordinates: every point and rect is in the ORIGINAL image's pixel coordinates (pixel (x,y) covers
// [x,x+1) x [y,y+1)), whatever the crop. A crop only chooses which part of that image render() gives back.
//
// Owner: builder A. The declarations below are the M3 contract tests/tst_document.cpp drives; do not change them.
#pragma once

#include <QColor>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

namespace rileyshot {

struct Annotation
{
    enum Kind {
        Pen,         // points: the freehand path
        Highlighter, // points: the freehand path
        Line,        // points: {from, to}
        Arrow,       // points: {from, to}; the head is at `to`
        Rectangle,   // rect
        Ellipse,     // rect: the ellipse's bounding box
        Text,        // position: the text box's top-left; text, textSize
        Marker,      // position: the circle's centre; number
        Blur,        // rect, snapped to whole pixels
        Pixelate     // rect, snapped to whole pixels
    };

    int id = -1;          // unique in its document, from 1 up, never reused (a redo brings back the same id)
    Kind kind = Pen;
    QColor colour;        // unused by Blur and Pixelate
    qreal width = 4;      // stroke width in image pixels (Pen, Highlighter, Line, Arrow, Rectangle, Ellipse)
    bool filled = false;  // Rectangle and Ellipse only
    QList<QPointF> points;
    QRectF rect;          // normalised (non-negative width and height)
    QPointF position;
    QString text;
    qreal textSize = 24;  // Text: the font's pixel size
    int number = 0;       // Marker: 1, 2, 3, ...
};

} // namespace rileyshot
