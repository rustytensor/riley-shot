// SPDX-License-Identifier: GPL-3.0-or-later
// The renderer behind Document::render() and its geometry helpers (bounds, hit distances, blur and pixelate).
// document.h, "How render() draws", is the specification every function here follows.
//
// Owner: builder A.
#pragma once

#include "annotation.h"

#include <QColor>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>

namespace rileyshot::render {

// What the renderer needs besides the annotations themselves.
struct Style
{
    QString fontFamily;
    QColor markerTextColour;
};

// Draws the annotations, bottom first, onto image (Format_RGB32, in image coordinates: no crop applied).
void paint(QImage &image, const QList<Annotation> &annotations, const Style &style);

// A Blur or Pixelate rect snapped to whole pixels (x, y, width and height each qRound-ed) and clipped to size.
QRect snapRect(const QRectF &rect, const QSize &size);

// Replaces the pixels of image inside rect (already snapped and clipped) by a blur that samples only inside rect.
void blurRect(QImage &image, const QRect &rect);
// Replaces the pixels of image inside rect by 10x10 blocks, each its rounded mean per channel.
void pixelateRect(QImage &image, const QRect &rect);

// annotationBounds as document.h defines it.
QRectF bounds(const Annotation &annotation, const Style &style);
// The text box of a Text annotation.
QRectF textBox(const Annotation &annotation, const QString &fontFamily);

// True when point hits the annotation (document.h, "Hit testing").
bool hits(const Annotation &annotation, const QPointF &point, const Style &style);

// The distance from point to the polyline through points (one point: the distance to it).
qreal distanceToPolyline(const QList<QPointF> &points, const QPointF &point);

} // namespace rileyshot::render
