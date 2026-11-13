// SPDX-License-Identifier: GPL-3.0-or-later
#include "compose.h"

#include <QPainter>

namespace rileyshot {

QImage compose(const RegionPlan &plan, const QHash<QString, QImage> &images)
{
    if (plan.isEmpty() || plan.size.isEmpty())
        return QImage();

    QImage result(plan.size, QImage::Format_ARGB32);
    result.fill(0);
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (const Piece &piece : plan.pieces) {
        const auto it = images.constFind(piece.output);
        if (it == images.constEnd() || it->isNull())
            continue;
        if (piece.source.size() == piece.target.size()) {
            painter.drawImage(piece.target.topLeft(), *it, piece.source);
        } else {
            const QImage scaled = it->copy(piece.source).scaled(piece.target.size(), Qt::IgnoreAspectRatio,
                                                               Qt::SmoothTransformation);
            painter.drawImage(piece.target.topLeft(), scaled);
        }
    }
    painter.end();
    return result;
}

} // namespace rileyshot
