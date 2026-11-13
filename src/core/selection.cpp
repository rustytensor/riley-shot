// SPDX-License-Identifier: GPL-3.0-or-later
#include "selection.h"

#include "region.h"

#include <QSet>

#include <algorithm>
#include <climits>
#include <cstdlib>

namespace rileyshot {

namespace {

// Edges of a rectangle: l, t inclusive; r, b exclusive (r = x + width, b = y + height).
struct Edges
{
    int l = 0, t = 0, r = 0, b = 0;
};

Edges edgesOf(const QRect &rect)
{
    return {rect.x(), rect.y(), rect.x() + rect.width(), rect.y() + rect.height()};
}

QRect rectOf(const Edges &e)
{
    return QRect(QPoint(e.l, e.t), QSize(e.r - e.l, e.b - e.t));
}

// The rectangle shifted (size kept) so it lies inside the bounds; unchanged when the bounds are null.
QRect shiftedInside(const QRect &rect, const QRect &bounds)
{
    if (bounds.isNull() || rect.isEmpty())
        return rect;
    const Edges bo = edgesOf(bounds);
    Edges e = edgesOf(rect);
    int dx = 0, dy = 0;
    if (e.r > bo.r)
        dx = bo.r - e.r;
    if (e.l + dx < bo.l)
        dx = bo.l - e.l;
    if (e.b > bo.b)
        dy = bo.b - e.b;
    if (e.t + dy < bo.t)
        dy = bo.t - e.t;
    return rect.translated(dx, dy);
}

int clampTo(int v, int lo, int hi)
{
    return std::clamp(v, lo, hi);
}

} // namespace

QRect layoutBounds(const QList<Output> &outputs)
{
    if (outputs.isEmpty())
        return QRect();
    Edges e = edgesOf(outputs.first().layoutRect());
    for (const Output &o : outputs) {
        const Edges r = edgesOf(o.layoutRect());
        e.l = std::min(e.l, r.l);
        e.t = std::min(e.t, r.t);
        e.r = std::max(e.r, r.r);
        e.b = std::max(e.b, r.b);
    }
    return rectOf(e);
}

// ---------------------------------------------------------------------------------------------------------------

void RegionSelection::setBounds(const QRect &bounds)
{
    m_bounds = bounds;
}

QRect RegionSelection::bounds() const
{
    return m_bounds;
}

QRect RegionSelection::rect() const
{
    return m_rect.isEmpty() ? QRect() : m_rect;
}

bool RegionSelection::isEmpty() const
{
    return m_rect.isEmpty();
}

void RegionSelection::setRect(const QRect &rect)
{
    const QRect r = rect.normalized();
    m_rect = r.isEmpty() ? QRect() : shiftedInside(r, m_bounds);
}

void RegionSelection::clear()
{
    m_rect = QRect();
}

Handle RegionSelection::handleAt(QPoint p) const
{
    if (isEmpty())
        return Handle::None;
    const Edges e = edgesOf(m_rect);
    const int g = kHandleGrab;
    const int x = p.x(), y = p.y();
    auto near = [g](int a, int b) { return std::abs(a - b) <= g; };

    if (near(x, e.l) && near(y, e.t))
        return Handle::TopLeft;
    if (near(x, e.r) && near(y, e.t))
        return Handle::TopRight;
    if (near(x, e.r) && near(y, e.b))
        return Handle::BottomRight;
    if (near(x, e.l) && near(y, e.b))
        return Handle::BottomLeft;

    const bool inX = e.l <= x && x <= e.r;
    const bool inY = e.t <= y && y <= e.b;
    if (near(y, e.t) && inX)
        return Handle::Top;
    if (near(y, e.b) && inX)
        return Handle::Bottom;
    if (near(x, e.l) && inY)
        return Handle::Left;
    if (near(x, e.r) && inY)
        return Handle::Right;

    if (e.l <= x && x < e.r && e.t <= y && y < e.b)
        return Handle::Move;
    return Handle::None;
}

void RegionSelection::press(QPoint p)
{
    m_pressPoint = p;
    m_pressRect = rect();
    m_drag = handleAt(p);
    if (m_drag == Handle::None) {
        m_drag = Handle::New;
        m_rect = QRect();
        m_pressRect = QRect();
    }
    m_dragging = true;
}

void RegionSelection::move(QPoint q)
{
    if (!m_dragging)
        return;

    const bool bounded = !m_bounds.isNull();
    const Edges bo = edgesOf(m_bounds);
    auto clampX = [&](int v) { return bounded ? clampTo(v, bo.l, bo.r) : v; };
    auto clampY = [&](int v) { return bounded ? clampTo(v, bo.t, bo.b) : v; };

    if (m_drag == Handle::New) {
        const int px = clampX(m_pressPoint.x()), py = clampY(m_pressPoint.y());
        const int qx = clampX(q.x()), qy = clampY(q.y());
        const Edges e{std::min(px, qx), std::min(py, qy), std::max(px, qx), std::max(py, qy)};
        const QRect r = rectOf(e);
        m_rect = r.isEmpty() ? QRect() : r;
        return;
    }

    const int dx = q.x() - m_pressPoint.x();
    const int dy = q.y() - m_pressPoint.y();

    if (m_drag == Handle::Move) {
        m_rect = shiftedInside(m_pressRect.translated(dx, dy), m_bounds);
        return;
    }

    Edges e = edgesOf(m_pressRect);
    switch (m_drag) {
    case Handle::TopLeft:
        e.l += dx;
        e.t += dy;
        break;
    case Handle::Top:
        e.t += dy;
        break;
    case Handle::TopRight:
        e.r += dx;
        e.t += dy;
        break;
    case Handle::Right:
        e.r += dx;
        break;
    case Handle::BottomRight:
        e.r += dx;
        e.b += dy;
        break;
    case Handle::Bottom:
        e.b += dy;
        break;
    case Handle::BottomLeft:
        e.l += dx;
        e.b += dy;
        break;
    case Handle::Left:
        e.l += dx;
        break;
    default:
        return;
    }
    e.l = clampX(e.l);
    e.r = clampX(e.r);
    e.t = clampY(e.t);
    e.b = clampY(e.b);
    if (e.l > e.r)
        std::swap(e.l, e.r);
    if (e.t > e.b)
        std::swap(e.t, e.b);
    const QRect r = rectOf(e);
    m_rect = r.isEmpty() ? QRect() : r;
}

void RegionSelection::release(QPoint q)
{
    if (!m_dragging)
        return;
    move(q);
    m_dragging = false;
    m_drag = Handle::None;
}

bool RegionSelection::dragging() const
{
    return m_dragging;
}

Handle RegionSelection::dragHandle() const
{
    return m_dragging ? m_drag : Handle::None;
}

bool RegionSelection::nudge(int dx, int dy)
{
    if (isEmpty())
        return false;
    m_rect = shiftedInside(m_rect.translated(dx, dy), m_bounds);
    return true;
}

bool RegionSelection::resizeBy(int dw, int dh)
{
    if (isEmpty())
        return false;
    Edges e = edgesOf(m_rect);
    e.r += dw;
    e.b += dh;
    if (!m_bounds.isNull()) {
        const Edges bo = edgesOf(m_bounds);
        e.r = std::min(e.r, bo.r);
        e.b = std::min(e.b, bo.b);
    }
    e.r = std::max(e.r, e.l + 1);
    e.b = std::max(e.b, e.t + 1);
    m_rect = rectOf(e);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------

QString sizeText(const QList<Output> &outputs, const QRect &rect)
{
    if (rect.isEmpty())
        return QString();
    const RegionPlan plan = planRegion(outputs, rect);
    if (plan.isEmpty())
        return QString();
    return QString::number(plan.size.width()) + QStringLiteral(" × ") + QString::number(plan.size.height());
}

namespace {

QSet<int> activeWorkspaces(const QList<Output> &outputs)
{
    QSet<int> set;
    for (const Output &o : outputs)
        set.insert(o.activeWorkspace);
    return set;
}

} // namespace

QList<Client> visibleClients(const QList<Client> &clients, const QList<Output> &outputs)
{
    const QSet<int> active = activeWorkspaces(outputs);
    QList<Client> out;
    for (const Client &c : clients)
        if (active.contains(c.workspace))
            out.append(c);
    return out;
}

int clientAt(const QList<Client> &clients, const QList<Output> &outputs, QPoint layout)
{
    const QSet<int> active = activeWorkspaces(outputs);
    int best = -1;
    int bestFocus = INT_MAX;
    for (qsizetype i = 0; i < clients.size(); ++i) {
        const Client &c = clients[i];
        if (!active.contains(c.workspace))
            continue;
        const Edges e = edgesOf(c.rect);
        if (!(e.l <= layout.x() && layout.x() < e.r && e.t <= layout.y() && layout.y() < e.b))
            continue;
        // An unknown focus history (-1) ranks below every known one.
        const int focus = c.focusHistoryId < 0 ? INT_MAX : c.focusHistoryId;
        if (best < 0 || focus < bestFocus) {
            best = int(i);
            bestFocus = focus;
        }
    }
    return best;
}

QString barOutputFor(const QList<Output> &outputs, QPointF cursor)
{
    if (outputs.isEmpty())
        return QString();
    if (const Output *o = outputAt(outputs, cursor))
        return o->name;
    return outputs.first().name;
}

} // namespace rileyshot
