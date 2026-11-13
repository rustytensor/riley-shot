// SPDX-License-Identifier: GPL-3.0-or-later
#include "documentcanvas.h"

#include <QCursor>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace rileyshot {

namespace {

constexpr qreal kMinZoom = 0.05;
constexpr qreal kMaxZoom = 8.0;
constexpr qreal kZoomStep = 1.25;
// The selection handles, in canvas (screen) pixels.
constexpr qreal kHandleSize = 8.0;

qreal clampZoom(qreal z)
{
    return std::clamp(z, kMinZoom, kMaxZoom);
}

} // namespace

struct DocumentCanvas::Private
{
    QPointer<Document> document;
    QList<QMetaObject::Connection> connections;
    qreal zoom = 1.0;
    bool fit = true;
    qreal availableWidth = 0;
    qreal availableHeight = 0;
    QColor accent{0xe9, 0xa0, 0x46};
    bool pressed = false;

    // Keeps zoom (when fitting) and the implicit size in step with the document and the room available.
    void relayout(DocumentCanvas *canvas);
};

void DocumentCanvas::Private::relayout(DocumentCanvas *canvas)
{
    Private *d = this;
    const QSize out = d->document ? d->document->outputSize() : QSize();
    if (d->fit && out.width() > 0 && out.height() > 0) {
        qreal z = 1.0;
        if (d->availableWidth > 0)
            z = std::min(z, d->availableWidth / out.width());
        if (d->availableHeight > 0)
            z = std::min(z, d->availableHeight / out.height());
        z = clampZoom(z);
        if (z != d->zoom) {
            d->zoom = z;
            emit canvas->zoomChanged();
        }
    }
    canvas->setImplicitSize(out.width() * d->zoom, out.height() * d->zoom);
    canvas->update();
}


DocumentCanvas::DocumentCanvas(QQuickItem *parent) : QQuickPaintedItem(parent), d(new Private)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAntialiasing(false);
    setOpaquePainting(true);
}

DocumentCanvas::~DocumentCanvas()
{
    for (const auto &c : d->connections)
        disconnect(c);
    delete d;
}

Document *DocumentCanvas::document() const
{
    return d->document;
}

void DocumentCanvas::setDocument(Document *document)
{
    if (d->document == document)
        return;
    for (const auto &c : d->connections)
        disconnect(c);
    d->connections.clear();
    d->document = document;
    d->pressed = false;
    if (document) {
        auto repaint = [this] { update(); };
        auto resize = [this] { d->relayout(this); update(); };
        auto cursor = [this] {
            setCursor(d->document && d->document->tool() == Document::Select ? Qt::ArrowCursor : Qt::CrossCursor);
        };
        cursor();
        d->connections << connect(document, &Document::toolChanged, this, cursor)
                       << connect(document, &Document::changed, this, repaint)
                       << connect(document, &Document::selectionChanged, this, repaint)
                       << connect(document, &Document::imageChanged, this, resize)
                       << connect(document, &Document::cropChanged, this, resize)
                       << connect(document, &QObject::destroyed, this, [this] {
                              d->document = nullptr;
                              d->pressed = false;
                              d->relayout(this);
                              update();
                          });
    }
    d->relayout(this);
    update();
    emit documentChanged();
}

qreal DocumentCanvas::zoom() const
{
    return d->zoom;
}

void DocumentCanvas::setZoom(qreal zoom)
{
    if (!std::isfinite(zoom) || zoom <= 0)
        return;
    setFit(false);
    const qreal z = clampZoom(zoom);
    if (z != d->zoom) {
        d->zoom = z;
        emit zoomChanged();
    }
    d->relayout(this);
    update();
}

bool DocumentCanvas::fit() const
{
    return d->fit;
}

void DocumentCanvas::setFit(bool on)
{
    if (d->fit == on)
        return;
    d->fit = on;
    emit fitChanged();
    d->relayout(this);
    update();
}

qreal DocumentCanvas::availableWidth() const
{
    return d->availableWidth;
}

void DocumentCanvas::setAvailableWidth(qreal width)
{
    if (d->availableWidth == width)
        return;
    d->availableWidth = width;
    emit availableSizeChanged();
    d->relayout(this);
}

qreal DocumentCanvas::availableHeight() const
{
    return d->availableHeight;
}

void DocumentCanvas::setAvailableHeight(qreal height)
{
    if (d->availableHeight == height)
        return;
    d->availableHeight = height;
    emit availableSizeChanged();
    d->relayout(this);
}

QColor DocumentCanvas::accent() const
{
    return d->accent;
}

void DocumentCanvas::setAccent(const QColor &colour)
{
    if (d->accent == colour)
        return;
    d->accent = colour;
    emit accentChanged();
    update();
}

QPointF DocumentCanvas::toImage(const QPointF &local) const
{
    const QPointF origin = d->document ? QPointF(d->document->cropRect().topLeft()) : QPointF();
    return origin + local / d->zoom;
}

void DocumentCanvas::zoomIn()
{
    setZoom(d->zoom * kZoomStep);
}

void DocumentCanvas::zoomOut()
{
    setZoom(d->zoom / kZoomStep);
}

void DocumentCanvas::paint(QPainter *painter)
{
    if (!d->document)
        return;
    const QImage image = d->document->render();
    if (image.isNull())
        return;
    const qreal z = d->zoom;
    painter->setRenderHint(QPainter::SmoothPixmapTransform, z < 1.0);
    painter->drawImage(QRectF(0, 0, image.width() * z, image.height() * z), image);

    if (d->document->selectedId() < 0)
        return;
    const QRectF bounds = d->document->selectedBounds();
    if (bounds.isNull())
        return;
    const QPointF origin = d->document->cropRect().topLeft();
    const QRectF r((bounds.topLeft() - origin) * z, bounds.size() * z);

    painter->setRenderHint(QPainter::Antialiasing, false);
    // The outline: the accent over a dark line, so it shows on light and dark shots alike.
    QPen under(QColor(0, 0, 0, 140), 3);
    under.setCosmetic(true);
    painter->setPen(under);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(r);
    QPen over(d->accent, 1);
    over.setCosmetic(true);
    painter->setPen(over);
    painter->drawRect(r);

    // The 8 handles: corners and edge midpoints.
    const qreal xs[3] = {r.left(), r.center().x(), r.right()};
    const qreal ys[3] = {r.top(), r.center().y(), r.bottom()};
    QPen edge(QColor(0, 0, 0, 160), 1);
    edge.setCosmetic(true);
    painter->setPen(edge);
    painter->setBrush(d->accent);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            if (i == 1 && j == 1)
                continue;
            painter->drawRect(QRectF(xs[i] - kHandleSize / 2, ys[j] - kHandleSize / 2, kHandleSize, kHandleSize));
        }
}

void DocumentCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !d->document) {
        event->ignore();
        return;
    }
    event->accept();
    setKeepMouseGrab(true);
    d->pressed = true;
    d->document->press(toImage(event->position()), int(event->modifiers()));
}

void DocumentCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (!d->pressed || !d->document) {
        event->ignore();
        return;
    }
    event->accept();
    d->document->move(toImage(event->position()), int(event->modifiers()));
}

void DocumentCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (!d->pressed || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    event->accept();
    d->pressed = false;
    setKeepMouseGrab(false);
    if (d->document)
        d->document->release(toImage(event->position()), int(event->modifiers()));
}

} // namespace rileyshot
