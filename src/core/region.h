// SPDX-License-Identifier: GPL-3.0-or-later
// Region planning: which device pixels of which outputs make up a layout rectangle, and where each lands in the
// result image. Pure: no I/O.
//
// A region is cut from each output's own device pixels (never from one image spanning the layout) and the pieces are
// joined at the highest scale among the outputs the region touches, so the sharpest screen is never scaled down.
//
// Owner: builder A (region.cpp). The declarations below are the contract the M1 gate tests; do not change them.
#pragma once

#include "output.h"

#include <QList>
#include <QRect>
#include <QSize>
#include <QString>

namespace rileyshot {

struct Piece
{
    QString output; // the output's name
    QRect source;   // device pixels in that output's grab: output.layoutToDeviceRect(region & output.layoutRect())
    QRect target;   // pixels in the result image; differs in size from source when the output's scale < plan scale
    bool operator==(const Piece &) const = default;
};

struct RegionPlan
{
    QRect layout;      // the region asked for, in layout coordinates
    double scale = 1.0; // the join scale: the highest scale among the outputs the region touches
    QSize size;        // result image size: (ceil(w * scale - eps), ceil(h * scale - eps)); QSize() when empty
    QList<Piece> pieces; // one per output the region touches, in the order of the outputs list
    bool isEmpty() const { return pieces.isEmpty(); }
};

// Plans a layout rectangle. For each output, in list order, whose layoutRect() intersects the region with a non-empty
// area: i = region & layoutRect(); source = layoutToDeviceRect(i); target, with r = region.topLeft() and S = scale:
//   left = floor((i.x - r.x) * S + eps), top = floor((i.y - r.y) * S + eps),
//   right = ceil((i.x + i.width - r.x) * S - eps), bottom = ceil((i.y + i.height - r.y) * S - eps),
// clamped to QRect(QPoint(0, 0), size). Parts of the region over no output are left out of every piece (they stay
// transparent in compose()). A region touching no output gives an empty plan (scale 1, size QSize()).
RegionPlan planRegion(const QList<Output> &outputs, const QRect &layoutRegion);

// A whole output: one piece, source = target = QRect(QPoint(0, 0), deviceSize()), scale = output.scale,
// layout = output.layoutRect(), size = deviceSize().
RegionPlan planOutput(const Output &output);

} // namespace rileyshot
