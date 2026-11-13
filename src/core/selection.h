// SPDX-License-Identifier: GPL-3.0-or-later
// The capture overlay's logic without any UI: the region being dragged and its handles, the keyboard nudges, the
// size label, which window or screen is under the pointer, and which screen gets the capture bar. Pure: no I/O, no
// Qt GUI, so the M2 gate tests every rule here offscreen with hand-worked values.
//
// Everything is in layout coordinates (output.h), as integers: the overlay turns a pointer position on one output
// into layout coordinates (its layoutPos plus the local logical position) before it comes here.
//
// Rectangles use edges: l = x, t = y, r = x + width, b = y + height (r and b exclusive, as in planRegion()).
//
// Owner: builder A (selection.cpp). The declarations below are the M2 contract tests/tst_selection.cpp tests;
// do not change them.
#pragma once

#include "output.h"
#include "window.h"

#include <QList>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QString>

namespace rileyshot {

// How close (in layout pixels, inclusive) the pointer must be to an edge or corner to pick up that handle.
inline constexpr int kHandleGrab = 8;

// The values are fixed: QML gets them as plain ints (CaptureController::handleAt) to pick cursor shapes.
enum class Handle {
    None = 0,
    Move = 1,        // inside the rectangle: the whole rectangle moves
    TopLeft = 2,
    Top = 3,
    TopRight = 4,
    Right = 5,
    BottomRight = 6,
    Bottom = 7,
    BottomLeft = 8,
    Left = 9,
    New = 10,        // only as dragHandle(): a press that started a new rectangle
};

// The bounding box of every output's layoutRect(). QRect() for no outputs.
QRect layoutBounds(const QList<Output> &outputs);

// The region being chosen. Starts empty; bounds default to QRect() which means "no clamping" until setBounds().
class RegionSelection
{
public:
    void setBounds(const QRect &bounds); // every result below is clamped to these (see press/move rules)
    QRect bounds() const;

    QRect rect() const;               // the current region; QRect() when there is none
    bool isEmpty() const;             // rect() has zero width or height
    void setRect(const QRect &rect);  // as is, then clamped to the bounds (moved, as a Move drag would be)
    void clear();

    // The handle under a layout point, tested in this order (the first that matches wins), g = kHandleGrab:
    //   1. corners: |x - cx| <= g and |y - cy| <= g, for TopLeft (l,t), TopRight (r,t), BottomRight (r,b),
    //      BottomLeft (l,b), in that order;
    //   2. edges: Top |y - t| <= g and l <= x <= r; Bottom |y - b| <= g and l <= x <= r;
    //      Left |x - l| <= g and t <= y <= b; Right |x - r| <= g and t <= y <= b, in that order;
    //   3. Move: l <= x < r and t <= y < b;
    //   4. None.
    // An empty selection gives None everywhere.
    Handle handleAt(QPoint p) const;

    // A drag. press() remembers the point P and the rectangle as it is (l0,t0,r0,b0), and picks the handle:
    // handleAt(P), or New when that is None (the old rectangle is dropped). move(Q) then sets, with d = Q - P:
    //   New:  the rectangle spanned by P and Q: l = min(Px,Qx), r = max(Px,Qx), t = min(Py,Qy), b = max(Py,Qy),
    //         with P and Q first clamped into the bounds (x to bounds l..r, y to t..b);
    //   Move: every edge + d, then the whole rectangle shifted (size kept) so it lies inside the bounds;
    //   a handle: only its edges + d (TopLeft: l and t; Top: t; TopRight: r and t; Right: r; BottomRight: r and b;
    //         Bottom: b; BottomLeft: l and b; Left: l), each edge clamped into the bounds, then normalised
    //         (l and r swapped when l > r, t and b when t > b), so dragging past the opposite edge flips it.
    // Every move() starts again from the rectangle at press time (moves do not add up). release(Q) is move(Q) and
    // ends the drag. move() and release() without a press do nothing. A drag that ends with zero width or height
    // leaves the selection empty.
    void press(QPoint p);
    void move(QPoint q);
    void release(QPoint q);
    bool dragging() const;
    Handle dragHandle() const;        // the handle of the drag in progress; None when not dragging

    // Keyboard steps. Both return false and change nothing when the selection is empty.
    // nudge: the rectangle moves by (dx, dy), size kept, then shifted back inside the bounds.
    // resizeBy: r += dw and b += dh (the top-left corner stays), each side kept at least 1 and r, b at most the
    // bounds' right and bottom edges.
    bool nudge(int dx, int dy);
    bool resizeBy(int dw, int dh);

private:
    QRect m_bounds;
    QRect m_rect;
    QRect m_pressRect;
    QPoint m_pressPoint;
    Handle m_drag = Handle::None;
    bool m_dragging = false;
};

// The size label: planRegion(outputs, rect).size as "W × H" (U+00D7 between single spaces), e.g. "500 × 375" for
// 400x300 layout pixels on DP-2 at scale 1.25. An empty plan gives "".
QString sizeText(const QList<Output> &outputs, const QRect &rect);

// The clients that can be on screen: those whose workspace is the activeWorkspace of one of the outputs, in list
// order.
QList<Client> visibleClients(const QList<Client> &clients, const QList<Output> &outputs);

// The window under a layout point: among the clients whose workspace is some output's activeWorkspace and whose
// rect contains the point (half-open: left/top inside, right/bottom outside), the one with the lowest
// focusHistoryId (the most recently focused is on top). Returns its index in `clients`, or -1.
int clientAt(const QList<Client> &clients, const QList<Output> &outputs, QPoint layout);

// The output that gets the capture bar: the one under the pointer (outputAt), or the first output when the pointer
// is over none of them. "" for no outputs.
QString barOutputFor(const QList<Output> &outputs, QPointF cursor);

} // namespace rileyshot
