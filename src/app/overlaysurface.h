// SPDX-License-Identifier: GPL-3.0-or-later
// One output's capture overlay: a QQuickView on that output's QScreen, made a LayerShellQt layer surface (scope
// "riley-shot", layer overlay, anchored to all four edges, exclusive zone -1, keyboard interactivity exclusive),
// showing RileyShot/Overlay.qml for that output. Every surface of one capture shares one QQmlEngine and one
// CaptureController (session.h).
//
// Owner: builder A.
#pragma once

#include "output.h"

#include <QObject>
#include <QRect>
#include <QString>

class QQmlEngine;
class QQuickItem;
class QQuickView;
class QScreen;

namespace rileyshot {

class CaptureController;

class OverlaySurface : public QObject
{
    Q_OBJECT

public:
    // Builds the view and loads Overlay.qml with its required properties; does not show it. isValid() says
    // whether the QML loaded.
    OverlaySurface(QQmlEngine *engine, CaptureController *controller, const Output &output, QScreen *screen,
                   QObject *parent = nullptr);
    ~OverlaySurface() override;

    bool isValid() const;
    QString errorString() const; // the QML errors, one per line, when !isValid()

    QString outputName() const;
    QQuickView *view() const;

    // Shows the surface. firstFrame() follows once, after its first frame is swapped.
    void show();
    void hide();

    // The window's size in device pixels: qRound(width * devicePixelRatio) by qRound(height * dpr).
    QSize deviceSize() const;
    double devicePixelRatio() const;

    // The item named "captureBar", or nullptr.
    QQuickItem *captureBar() const;
    // Whether the capture bar is shown on this surface.
    bool hasVisibleBar() const;
    // The capture bar's rect in this output's device pixels, rounded outwards; QRect() without a visible bar.
    QRect barDeviceRect() const;

signals:
    void firstFrame();

private:
    Output m_output;
    QQuickView *m_view = nullptr;
    QString m_error;
    bool m_framed = false;
};

} // namespace rileyshot
