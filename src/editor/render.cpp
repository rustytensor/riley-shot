// SPDX-License-Identifier: GPL-3.0-or-later
#include "render.h"

#include <QFont>
#include <QFontMetricsF>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace rileyshot::render {

namespace {

const qreal kMarkerRadius = 14;
const int kMarkerFontPixels = 16;
const int kBlurRadius = 8;
const int kBlurPasses = 2;
const int kPixelBlock = 10;

QFont textFont(const QString &family, qreal pixelSize)
{
    QFont font(family);
    font.setPixelSize(std::max(1, qRound(pixelSize)));
    return font;
}

QFont markerFont(const QString &family)
{
    QFont font(family);
    font.setPixelSize(kMarkerFontPixels);
    font.setBold(true);
    return font;
}

QPainterPath polylinePath(const QList<QPointF> &points)
{
    QPainterPath path;
    if (points.isEmpty())
        return path;
    path.moveTo(points.first());
    for (qsizetype i = 1; i < points.size(); ++i)
        path.lineTo(points.at(i));
    return path;
}

// A single-point stroke: a round dot of the stroke's width.
void paintDot(QPainter &p, const QPointF &centre, const QColor &colour, qreal width)
{
    p.setPen(Qt::NoPen);
    p.setBrush(colour);
    p.drawEllipse(centre, width / 2, width / 2);
}

void paintStroke(QPainter &p, const Annotation &a, QColor colour)
{
    if (a.points.isEmpty())
        return;
    if (a.points.size() == 1) {
        paintDot(p, a.points.first(), colour, a.width);
        return;
    }
    p.setPen(QPen(colour, a.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    // One path stroked once: the stroker's outline is filled with the winding rule, so where the path crosses itself
    // the colour is applied once.
    p.drawPath(polylinePath(a.points));
}

void paintLine(QPainter &p, const Annotation &a)
{
    if (a.points.size() < 2)
        return;
    p.setPen(QPen(a.colour, a.width, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawLine(a.points.at(0), a.points.at(1));
}

void paintArrow(QPainter &p, const Annotation &a)
{
    if (a.points.size() < 2)
        return;
    const QPointF from = a.points.at(0), to = a.points.at(1);
    const QPointF d = to - from;
    const qreal length = std::hypot(d.x(), d.y());
    if (length <= 0)
        return;
    const QPointF u = d / length;          // along the arrow
    const QPointF n(-u.y(), u.x());         // perpendicular
    qreal L = std::max(4 * a.width, qreal(12));
    const qreal H = std::max(2 * a.width, qreal(6));
    if (length < L)
        L = length;
    // The shaft, flat-capped, ending L/2 back from the tip (inside the head).
    const QPointF shaftEnd = to - u * (L / 2);
    if (QLineF(from, shaftEnd).length() > 0) {
        p.setPen(QPen(a.colour, a.width, Qt::SolidLine, Qt::FlatCap));
        p.setBrush(Qt::NoBrush);
        p.drawLine(from, shaftEnd);
    }
    // The head: tip at `to`, base of half-width H at distance L from it.
    const QPointF base = to - u * L;
    const QPolygonF head{to, base + n * H, base - n * H};
    p.setPen(Qt::NoPen);
    p.setBrush(a.colour);
    p.drawPolygon(head);
}

void paintRectangle(QPainter &p, const Annotation &a)
{
    p.setPen(QPen(a.colour, a.width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    p.setBrush(a.filled ? QBrush(a.colour) : QBrush(Qt::NoBrush));
    p.drawRect(a.rect.normalized());
}

void paintEllipse(QPainter &p, const Annotation &a)
{
    p.setPen(QPen(a.colour, a.width));
    p.setBrush(a.filled ? QBrush(a.colour) : QBrush(Qt::NoBrush));
    p.drawEllipse(a.rect.normalized());
}

void paintText(QPainter &p, const Annotation &a, const Style &style)
{
    const QFont font = textFont(style.fontFamily, a.textSize);
    const QFontMetricsF fm(font);
    p.setFont(font);
    p.setPen(a.colour);
    p.setBrush(Qt::NoBrush);
    const QStringList lines = a.text.split(QLatin1Char('\n'));
    for (qsizetype i = 0; i < lines.size(); ++i)
        p.drawText(QPointF(a.position.x(), a.position.y() + fm.ascent() + i * fm.lineSpacing()), lines.at(i));
}

void paintMarker(QPainter &p, const Annotation &a, const Style &style)
{
    p.setPen(Qt::NoPen);
    p.setBrush(a.colour);
    p.drawEllipse(a.position, kMarkerRadius, kMarkerRadius);
    const QFont font = markerFont(style.fontFamily);
    const QFontMetricsF fm(font);
    const QString number = QString::number(a.number);
    p.setFont(font);
    p.setPen(style.markerTextColour.isValid() ? style.markerTextColour : QColor(Qt::white));
    p.setBrush(Qt::NoBrush);
    p.drawText(QPointF(a.position.x() - fm.horizontalAdvance(number) / 2,
                       a.position.y() + (fm.ascent() - fm.descent()) / 2),
               number);
}

// One box blur pass of radius r along a line of n values (stride apart), clamped (edge values repeat).
void boxLine(int *data, int n, int stride, int r, std::vector<int> &scratch)
{
    if (n <= 0)
        return;
    scratch.resize(size_t(n));
    for (int i = 0; i < n; ++i)
        scratch[size_t(i)] = data[i * stride];
    auto at = [&](int i) { return scratch[size_t(std::clamp(i, 0, n - 1))]; };
    const int window = 2 * r + 1;
    int sum = 0;
    for (int i = -r; i <= r; ++i)
        sum += at(i);
    for (int i = 0; i < n; ++i) {
        data[i * stride] = (sum + window / 2) / window;
        sum += at(i + r + 1) - at(i - r);
    }
}

} // namespace

QRect snapRect(const QRectF &rect, const QSize &size)
{
    const QRectF r = rect.normalized();
    const QRect snapped(qRound(r.x()), qRound(r.y()), qRound(r.width()), qRound(r.height()));
    return snapped & QRect(QPoint(0, 0), size);
}

void blurRect(QImage &image, const QRect &rect)
{
    const QRect r = rect & image.rect();
    if (r.isEmpty())
        return;
    const int w = r.width(), h = r.height();
    std::vector<int> channel[3];
    for (auto &c : channel)
        c.resize(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(r.y() + y)) + r.x();
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * size_t(w) + size_t(x);
            channel[0][i] = qRed(line[x]);
            channel[1][i] = qGreen(line[x]);
            channel[2][i] = qBlue(line[x]);
        }
    }
    std::vector<int> scratch;
    for (auto &c : channel)
        for (int pass = 0; pass < kBlurPasses; ++pass) {
            for (int y = 0; y < h; ++y)
                boxLine(c.data() + size_t(y) * size_t(w), w, 1, kBlurRadius, scratch);
            for (int x = 0; x < w; ++x)
                boxLine(c.data() + x, h, w, kBlurRadius, scratch);
        }
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(r.y() + y)) + r.x();
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * size_t(w) + size_t(x);
            line[x] = qRgb(channel[0][i], channel[1][i], channel[2][i]);
        }
    }
}

void pixelateRect(QImage &image, const QRect &rect)
{
    const QRect r = rect & image.rect();
    if (r.isEmpty())
        return;
    for (int by = r.top(); by <= r.bottom(); by += kPixelBlock)
        for (int bx = r.left(); bx <= r.right(); bx += kPixelBlock) {
            const int x1 = std::min(bx + kPixelBlock, r.right() + 1), y1 = std::min(by + kPixelBlock, r.bottom() + 1);
            const int n = (x1 - bx) * (y1 - by);
            long long sr = 0, sg = 0, sb = 0;
            for (int y = by; y < y1; ++y) {
                const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
                for (int x = bx; x < x1; ++x) {
                    sr += qRed(line[x]);
                    sg += qGreen(line[x]);
                    sb += qBlue(line[x]);
                }
            }
            const QRgb mean = qRgb(int((sr + n / 2) / n), int((sg + n / 2) / n), int((sb + n / 2) / n));
            for (int y = by; y < y1; ++y) {
                QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
                for (int x = bx; x < x1; ++x)
                    line[x] = mean;
            }
        }
}

void paint(QImage &image, const QList<Annotation> &annotations, const Style &style)
{
    QPainter p;
    auto begin = [&] {
        p.begin(&image);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    };
    begin();
    for (const Annotation &a : annotations) {
        switch (a.kind) {
        case Annotation::Pen:
            paintStroke(p, a, a.colour);
            break;
        case Annotation::Highlighter: {
            QColor c = a.colour;
            c.setAlpha(128);
            paintStroke(p, a, c);
            break;
        }
        case Annotation::Line:
            paintLine(p, a);
            break;
        case Annotation::Arrow:
            paintArrow(p, a);
            break;
        case Annotation::Rectangle:
            paintRectangle(p, a);
            break;
        case Annotation::Ellipse:
            paintEllipse(p, a);
            break;
        case Annotation::Text:
            paintText(p, a, style);
            break;
        case Annotation::Marker:
            paintMarker(p, a, style);
            break;
        case Annotation::Blur:
        case Annotation::Pixelate: {
            // These work on the pixels drawn so far: finish the painter, change the pixels, carry on.
            const QRect r = snapRect(a.rect, image.size());
            if (r.isEmpty())
                break;
            p.end();
            if (a.kind == Annotation::Blur)
                blurRect(image, r);
            else
                pixelateRect(image, r);
            begin();
            break;
        }
        }
    }
    p.end();
}

QRectF textBox(const Annotation &a, const QString &fontFamily)
{
    const QFontMetricsF fm(textFont(fontFamily, a.textSize));
    const QStringList lines = a.text.split(QLatin1Char('\n'));
    qreal widest = 0;
    for (const QString &line : lines)
        widest = std::max(widest, fm.horizontalAdvance(line));
    return QRectF(a.position, QSizeF(widest, fm.height() + (lines.size() - 1) * fm.lineSpacing()));
}

QRectF bounds(const Annotation &a, const Style &style)
{
    switch (a.kind) {
    case Annotation::Pen:
    case Annotation::Highlighter:
    case Annotation::Line:
    case Annotation::Arrow: {
        if (a.points.isEmpty())
            return QRectF();
        qreal x0 = a.points.first().x(), x1 = x0, y0 = a.points.first().y(), y1 = y0;
        for (const QPointF &pt : a.points) {
            x0 = std::min(x0, pt.x());
            x1 = std::max(x1, pt.x());
            y0 = std::min(y0, pt.y());
            y1 = std::max(y1, pt.y());
        }
        return QRectF(x0, y0, x1 - x0, y1 - y0);
    }
    case Annotation::Rectangle:
    case Annotation::Ellipse:
    case Annotation::Blur:
    case Annotation::Pixelate:
        return a.rect.normalized();
    case Annotation::Text:
        return textBox(a, style.fontFamily);
    case Annotation::Marker:
        return QRectF(a.position.x() - kMarkerRadius, a.position.y() - kMarkerRadius, 2 * kMarkerRadius,
                      2 * kMarkerRadius);
    }
    return QRectF();
}

qreal distanceToPolyline(const QList<QPointF> &points, const QPointF &point)
{
    if (points.isEmpty())
        return std::numeric_limits<qreal>::infinity();
    auto dist = [](const QPointF &a, const QPointF &b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); };
    qreal best = dist(points.first(), point);
    for (qsizetype i = 1; i < points.size(); ++i) {
        const QPointF a = points.at(i - 1), b = points.at(i);
        const QPointF ab = b - a;
        const qreal len2 = ab.x() * ab.x() + ab.y() * ab.y();
        qreal t = 0;
        if (len2 > 0)
            t = std::clamp(((point.x() - a.x()) * ab.x() + (point.y() - a.y()) * ab.y()) / len2, qreal(0), qreal(1));
        best = std::min(best, dist(a + ab * t, point));
    }
    return best;
}

bool hits(const Annotation &a, const QPointF &point, const Style &style)
{
    switch (a.kind) {
    case Annotation::Pen:
    case Annotation::Highlighter:
    case Annotation::Line:
    case Annotation::Arrow:
        return distanceToPolyline(a.points, point) <= a.width / 2 + 3;
    case Annotation::Rectangle:
    case Annotation::Ellipse: {
        const qreal g = a.width / 2 + 3;
        return bounds(a, style).adjusted(-g, -g, g, g).contains(point);
    }
    case Annotation::Text:
    case Annotation::Marker:
    case Annotation::Blur:
    case Annotation::Pixelate:
        return bounds(a, style).adjusted(-3, -3, 3, 3).contains(point);
    }
    return false;
}

} // namespace rileyshot::render
