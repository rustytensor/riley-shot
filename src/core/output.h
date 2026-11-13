// SPDX-License-Identifier: GPL-3.0-or-later
// Outputs (screens) as Hyprland reports them, and the mapping between layout coordinates and each output's own
// device pixels. Pure: no I/O, no Qt GUI.
//
// Terms used throughout riley-shot:
//   layout coordinates  Hyprland's global logical space: hyprctl's x,y for outputs and at/size for windows.
//   device pixels       one output's own pixels, upright: exactly the image `grim -o NAME` gives. grim already
//                       applies the output transform, so the device image is the mode size with width and height
//                       swapped for 90/270-degree transforms, and layout->device is a translation and a scale only.
//   logical size        the device size divided by the scale: the output's size in layout coordinates.
//
// Owner: builder A (output.cpp). The declarations below are the contract the M1 gate tests; do not change them.
#pragma once

#include <QByteArray>
#include <QList>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>

namespace rileyshot {

// Rounding guard for float products such as 3 * 1.6 = 4.800000000000001. Every floor() in riley-shot's geometry is
// floor(v + kEpsilon) and every ceil() is ceil(v - kEpsilon).
inline constexpr double kEpsilon = 1e-6;

struct Output
{
    QString name;          // "DP-2"
    QPoint layoutPos;      // hyprctl "x","y": top-left corner in layout coordinates
    QSize modeSize;        // hyprctl "width","height": the mode in device pixels, before the transform
    double scale = 1.0;    // hyprctl "scale"
    int transform = 0;     // hyprctl "transform", wl_output.transform 0..7; 1, 3, 5, 7 turn the output by 90 or 270
    int activeWorkspace = 0; // hyprctl "activeWorkspace"."id"

    // modeSize, with width and height swapped when transform is odd. The size of grim -o NAME's image.
    QSize deviceSize() const;
    // deviceSize() / scale, each side rounded to the nearest integer (std::lround).
    QSize logicalSize() const;
    // QRect(layoutPos, logicalSize()).
    QRect layoutRect() const;

    // (layout - layoutPos) * scale. Not clamped: points outside the output map outside the device image.
    QPointF layoutToDevice(QPointF layout) const;
    // device / scale + layoutPos. The exact inverse of layoutToDevice.
    QPointF deviceToLayout(QPointF device) const;
    // The device pixels a layout rectangle covers ("covering rule"): with d = layoutToDevice,
    //   left = floor(d(topLeft).x + eps), top = floor(d(topLeft).y + eps),
    //   right = ceil(d(bottomRight).x - eps), bottom = ceil(d(bottomRight).y - eps)   (right/bottom exclusive;
    //   bottomRight here means x + width, y + height of the QRectF),
    // then clamped to QRect(QPoint(0, 0), deviceSize()). Returns QRect() (empty) when nothing is left.
    QRect layoutToDeviceRect(const QRectF &layout) const;

    bool operator==(const Output &) const = default;
};

// Parses `hyprctl monitors -j`. Outputs come back in the order the JSON lists them; entries with "disabled": true are
// left out. On malformed JSON (not an array, or an entry missing name/width/height/x/y/scale/transform) it returns an
// empty list and, when error is given, sets *error to a one-line reason.
QList<Output> parseMonitors(const QByteArray &json, QString *error = nullptr);

// The output whose layoutRect() contains the layout point (half-open: left/top edges inside, right/bottom outside),
// or nullptr when the point is in a gap between outputs.
const Output *outputAt(const QList<Output> &outputs, QPointF layout);

// Index of the output with this name, or -1.
int findOutput(const QList<Output> &outputs, const QString &name);

} // namespace rileyshot
