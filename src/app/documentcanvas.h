// SPDX-License-Identifier: GPL-3.0-or-later
// The editor's canvas: shows Document::render() at a zoom, draws the selection outline and handles over it, and turns
// pointer events into Document::press/move/release in image coordinates.
//
//   - Size: implicitWidth = document.outputSize.width * zoom, implicitHeight = document.outputSize.height * zoom
//     (Editor.qml gives it width: implicitWidth, height: implicitHeight).
//   - fit (default true): zoom = min(1, availableWidth / outputSize.width, availableHeight / outputSize.height),
//     kept up to date when the available size, the image or the crop changes. Never enlarged above 1 by fitting.
//     Setting zoom by hand sets fit false. zoomIn(): zoom * 1.25; zoomOut(): zoom / 1.25 (both set fit false);
//     zoom is kept within [0.05, 8].
//   - toImage(local) = cropRect.topLeft + local / zoom, local in this item's coordinates. Every left-button press,
//     move while pressed, and release goes to document.press/move/release(toImage(pos), modifiers).
//   - Paints render() scaled by zoom (smooth below 1, nearest pixel at 1 and above), then, when
//     document.selectedId is not -1, the selectedBounds outline and its 8 handles in the theme accent. Those are
//     never part of render() or anything saved.
//
// Owner: builder B (documentcanvas.cpp). The declarations below are the M3 contract tests/tst_editor_qml.cpp drives;
// do not change them.
#pragma once

#include "document.h"

#include <QColor>
#include <QPointF>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace rileyshot {

class DocumentCanvas : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(rileyshot::Document *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    Q_PROPERTY(bool fit READ fit WRITE setFit NOTIFY fitChanged)
    // The room the canvas may use for fitting (Editor.qml binds them to its "viewport" item's width and height).
    Q_PROPERTY(qreal availableWidth READ availableWidth WRITE setAvailableWidth NOTIFY availableSizeChanged)
    Q_PROPERTY(qreal availableHeight READ availableHeight WRITE setAvailableHeight NOTIFY availableSizeChanged)
    // Colour of the selection outline and handles.
    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY accentChanged)

public:
    explicit DocumentCanvas(QQuickItem *parent = nullptr);
    ~DocumentCanvas() override;

    Document *document() const;
    void setDocument(Document *document);
    qreal zoom() const;
    void setZoom(qreal zoom);
    bool fit() const;
    void setFit(bool on);
    qreal availableWidth() const;
    void setAvailableWidth(qreal width);
    qreal availableHeight() const;
    void setAvailableHeight(qreal height);
    QColor accent() const;
    void setAccent(const QColor &colour);

    Q_INVOKABLE QPointF toImage(const QPointF &local) const;
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();

    void paint(QPainter *painter) override;

signals:
    void documentChanged();
    void zoomChanged();
    void fitChanged();
    void availableSizeChanged();
    void accentChanged();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    struct Private;
    Private *d;
};

} // namespace rileyshot
