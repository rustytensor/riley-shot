// SPDX-License-Identifier: GPL-3.0-or-later
// Joins a region plan's pieces, cut from each output's grab, into one image.
//
// Owner: builder A (compose.cpp). The declaration below is the contract the M1 gate tests; do not change it.
#pragma once

#include "region.h"

#include <QHash>
#include <QImage>
#include <QString>

namespace rileyshot {

// The result is a QImage of plan.size in QImage::Format_ARGB32, first filled with 0 (transparent). Then each piece,
// in plan order, is drawn with QPainter::CompositionMode_Source:
//   - source size == target size: the source pixels are copied exactly, no filtering;
//   - otherwise: images[piece.output].copy(piece.source).scaled(piece.target.size(), Qt::IgnoreAspectRatio,
//     Qt::SmoothTransformation), copied exactly at piece.target.topLeft().
// A piece whose output has no image in `images` stays transparent. An empty plan gives a null QImage.
QImage compose(const RegionPlan &plan, const QHash<QString, QImage> &images);

} // namespace rileyshot
