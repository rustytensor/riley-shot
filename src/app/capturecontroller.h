// SPDX-License-Identifier: GPL-3.0-or-later
// The capture overlay's one controller, shared by every output's surface. QML (src/qml/Overlay.qml and
// CaptureBar.qml) draws from its properties and calls its invokables; C++ (session.cpp) fills it before any surface
// shows and quits when it emits finished(). It holds the frozen grabs and turns a choice (region, screen, window)
// into an image through the M1 core (planRegion/planOutput/planWindow, compose), then delivers it (deliver.h).
//
// Coordinates: every x, y here is in layout coordinates (output.h). An overlay converts its local logical position
// with layoutX + localX, layoutY + localY. Real values are floored (std::floor) to integers before they reach
// RegionSelection.
//
// Owner: builder A (capturecontroller.cpp). The declarations below are the M2 contract tests/tst_controller.cpp and
// tests/tst_overlay_qml.cpp drive; do not change them.
#pragma once

#include "cli.h"
#include "config.h"
#include "output.h"
#include "region.h"
#include "selection.h"
#include "theme.h"
#include "window.h"

#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;

namespace rileyshot {

class CaptureController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("riley-shot makes the capture controller")

    // Region, Screen or Window (CaptureMode). Changing it ends any drag and clears the hover; the region stays.
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged)
    // The capture bar's "include pointer" toggle; starts at Config::pointer.
    Q_PROPERTY(bool pointer READ pointer WRITE setPointer NOTIFY pointerChanged)
    // True when started without --region/--screen/--window (setStartMode(Mode::Bar)).
    Q_PROPERTY(bool barVisible READ barVisible NOTIFY barVisibleChanged)
    // The output whose surface shows the bar (barOutputFor at setCursor()); "" before setOutputs().
    Q_PROPERTY(QString barOutput READ barOutput NOTIFY barOutputChanged)
    // The region in layout coordinates (RegionSelection::rect()); QRect() when there is none.
    Q_PROPERTY(QRect selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    // sizeText(outputs, selection): "500 × 375"; "" without a selection.
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY selectionChanged)
    // Screen mode: the name of the output under the pointer ("" in a gap or in another mode).
    Q_PROPERTY(QString hoverOutput READ hoverOutput NOTIFY hoverChanged)
    // Window mode: the rect (layout coordinates) of clientAt() under the pointer; QRect() when none or in another
    // mode.
    Q_PROPERTY(QRect hoverWindow READ hoverWindow NOTIFY hoverChanged)
    // Whether capture() would make an image now: Region: hasSelection; Screen: always (with outputs); Window: a
    // hovered window.
    Q_PROPERTY(bool canCapture READ canCapture NOTIFY canCaptureChanged)
    // The Riley theme for QML: keys base surface0 surface1 text subtext accent mauve lavender teal peach green red
    // yellow (QColor values) and font (QString), from setTheme().
    Q_PROPERTY(QVariantMap theme READ theme NOTIFY themeChanged)

public:
    enum CaptureMode { Region = 0, Screen = 1, Window = 2 };
    Q_ENUM(CaptureMode)
    // finished()'s value, which is also riley-shot capture's exit code.
    enum Result { Captured = 0, Cancelled = 1, TimedOut = 3, Failed = 4 };
    Q_ENUM(Result)

    explicit CaptureController(QObject *parent = nullptr);
    ~CaptureController() override;

    // --- set up from C++, before any surface shows -----------------------------------------------------------
    // The outputs the overlay covers (already limited by --outputs). Sets the region bounds to layoutBounds().
    void setOutputs(const QList<Output> &outputs);
    QList<Output> outputs() const;
    // The frozen grabs (output name -> device-size image, no pointer) and, optionally, grabs with the pointer drawn
    // (grim -c). capture() uses pointerImages when pointer is on and pointerImages is not empty, else images.
    // Surfaces always show `images`.
    void setImages(const QHash<QString, QImage> &images, const QHash<QString, QImage> &pointerImages = {});
    QImage image(const QString &output) const; // images[output], or a null QImage
    void setClients(const QList<Client> &clients);
    // Also sets pointer to config.pointer.
    void setConfig(const Config &config);
    void setTheme(const Theme &theme);
    // Mode::Bar: mode Region and barVisible true. Mode::Region/Screen/Window: that mode and barVisible false.
    void setStartMode(Mode mode);
    // The pointer position when riley-shot started: barOutput = barOutputFor(outputs, cursor).
    void setCursor(QPointF layout);
    // The copy program and its arguments; default copyCommandFromEnvironment().
    void setCopyCommand(const QStringList &command);
    // The home folder for Config::resolvedSaveFolder(); default QDir::homePath().
    void setHome(const QString &home);
    // The time used for the saved file's name; an invalid QDateTime (the default) means the time of capture().
    void setNow(const QDateTime &now);

    // Adds the image provider "frozen" to the engine (the engine owns it): image://frozen/<output name> gives
    // image(output) at its device size. Overlay.qml shows it with source "image://frozen/" + outputName.
    void installImageProvider(QQmlEngine *engine);

    // --- read by QML -------------------------------------------------------------------------------------------
    int mode() const;
    void setMode(int mode); // values outside 0..2 are ignored
    bool pointer() const;
    void setPointer(bool on);
    bool barVisible() const;
    QString barOutput() const;
    QRect selection() const;
    bool hasSelection() const;
    QString sizeText() const;
    QString hoverOutput() const;
    QRect hoverWindow() const;
    bool canCapture() const;
    QVariantMap theme() const;

    // --- called by QML -----------------------------------------------------------------------------------------
    // Region mode: RegionSelection::press/move/release at the floored point.
    // Screen mode: move sets hoverOutput (outputAt); a press and a release on the same output capture it
    //   (planOutput).
    // Window mode: move sets hoverWindow (clientAt); a press and a release on the same window capture it
    //   (planWindow).
    // pointerMoved is called with and without a button down. modifiers are Qt::KeyboardModifiers as an int.
    Q_INVOKABLE void pointerPressed(qreal x, qreal y, int modifiers = 0);
    Q_INVOKABLE void pointerMoved(qreal x, qreal y, int modifiers = 0);
    Q_INVOKABLE void pointerReleased(qreal x, qreal y, int modifiers = 0);
    // Keys (key is a Qt::Key, modifiers Qt::KeyboardModifiers as an int). Returns whether the key was used:
    //   Escape: cancel(); true.
    //   Return or Enter: capture() when canCapture; true either way.
    //   Left/Right/Up/Down in Region mode with a selection: without Shift nudge by 1 layout pixel in that
    //     direction; with Shift resizeBy (Right +1 width, Left -1 width, Down +1 height, Up -1 height); true.
    //   Anything else, or an arrow without a selection or outside Region mode: false.
    Q_INVOKABLE bool keyPressed(int key, int modifiers = 0);
    // RegionSelection::handleAt as an int (Handle values), for cursor shapes.
    Q_INVOKABLE int handleAt(qreal x, qreal y) const;
    // Makes the image for the current choice and delivers it. Region: planRegion(outputs, selection); Screen:
    // planOutput of hoverOutput, or of barOutput when nothing is hovered; Window: planWindow of the hovered window.
    // Nothing happens when that plan is empty. Otherwise: image = compose(plan, the grabs chosen by `pointer`);
    // then, in this order, copyImage(image, copy command) when Config::copyOnCapture, and saveImage(image,
    // resolvedSaveFolder(home), fileNamePattern, now, "png") when Config::saveOnCapture; finished(Captured), or
    // finished(Failed) when the copy or the save fails.
    Q_INVOKABLE void capture();
    // finished(Cancelled). The close button and Escape.
    Q_INVOKABLE void cancel();
    // finished(TimedOut). The --exit-after-ms timer.
    void expire();

    // --- results, for the session and the tests ----------------------------------------------------------------
    bool isFinished() const;
    QImage result() const;      // the composed image of the last capture(); null before
    RegionPlan resultPlan() const;
    QString savedPath() const;  // the file saveImage wrote; "" when none
    QString error() const;      // one line when finished(Failed)

signals:
    void modeChanged();
    void pointerChanged();
    void barVisibleChanged();
    void barOutputChanged();
    void selectionChanged();
    void hoverChanged();
    void canCaptureChanged();
    void themeChanged();
    // Emitted exactly once: later capture()/cancel()/expire() calls do nothing.
    void finished(int result);

private:
    struct Private;
    Private *d;
};

} // namespace rileyshot
