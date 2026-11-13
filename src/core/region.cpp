// SPDX-License-Identifier: GPL-3.0-or-later
#include "region.h"

#include <algorithm>
#include <cmath>

namespace rileyshot {

RegionPlan planRegion(const QList<Output> &outputs, const QRect &layoutRegion)
{
    RegionPlan plan;
    plan.layout = layoutRegion;
    if (layoutRegion.isEmpty())
        return plan;

    // The join scale: the highest scale among the outputs the region touches with a non-empty area.
    QList<const Output *> touched;
    double scale = 0.0;
    for (const Output &o : outputs) {
        const QRect i = layoutRegion & o.layoutRect();
        if (i.isEmpty())
            continue;
        touched.append(&o);
        scale = std::max(scale, o.scale);
    }
    if (touched.isEmpty())
        return plan;

    plan.scale = scale;
    plan.size = QSize(int(std::ceil(layoutRegion.width() * scale - kEpsilon)),
                      int(std::ceil(layoutRegion.height() * scale - kEpsilon)));
    const QPoint r = layoutRegion.topLeft();
    for (const Output *o : touched) {
        const QRect i = layoutRegion & o->layoutRect();
        const QRect source = o->layoutToDeviceRect(QRectF(i));
        if (source.isEmpty())
            continue;
        const double left = std::max(0.0, std::floor((i.x() - r.x()) * scale + kEpsilon));
        const double top = std::max(0.0, std::floor((i.y() - r.y()) * scale + kEpsilon));
        const double right = std::min(double(plan.size.width()),
                                      std::ceil((i.x() + i.width() - r.x()) * scale - kEpsilon));
        const double bottom = std::min(double(plan.size.height()),
                                       std::ceil((i.y() + i.height() - r.y()) * scale - kEpsilon));
        if (right <= left || bottom <= top)
            continue;
        plan.pieces.append(Piece{o->name, source,
                                 QRect(int(left), int(top), int(right - left), int(bottom - top))});
    }
    if (plan.pieces.isEmpty()) {
        plan.scale = 1.0;
        plan.size = QSize();
    }
    return plan;
}

RegionPlan planOutput(const Output &output)
{
    RegionPlan plan;
    plan.layout = output.layoutRect();
    plan.scale = output.scale;
    plan.size = output.deviceSize();
    const QRect whole(QPoint(0, 0), plan.size);
    plan.pieces.append(Piece{output.name, whole, whole});
    return plan;
}

} // namespace rileyshot
