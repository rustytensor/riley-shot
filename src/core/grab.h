// SPDX-License-Identifier: GPL-3.0-or-later
// Grabbing outputs: grim once per output, all at the same time, each at the output's own device pixels.
// Also runs hyprctl, the one other program the capture core asks.
//
// Owner: builder B (grab.cpp). The declarations below are the contract the M1 gate tests; do not change them.
#pragma once

#include "output.h"

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

namespace rileyshot {

struct GrabOptions
{
    bool pointer = false;              // draw the pointer into the grab (grim -c)
    QString grim = QStringLiteral("grim"); // the grim program; the gate points this at a fake
    int timeoutMs = 5000;              // per grim process
};

// grim's arguments for one output, exactly:
//   pointer false: {"-t", "ppm", "-o", output, "-"}
//   pointer true:  {"-c", "-t", "ppm", "-o", output, "-"}
// (PPM on stdout: no PNG encode or decode on the capture path.)
QStringList grimArguments(const QString &output, bool pointer);

struct GrabResult
{
    QHash<QString, QImage> images; // output name -> its grab, deviceSize() exactly
    QString error;                 // empty on success; otherwise one line naming the output and what went wrong
    bool ok() const { return error.isEmpty(); }
};

// Starts one grim per output at once (not one after the other), then waits for all. A grim that fails, times out,
// gives an image that does not load, or gives an image whose size is not output.deviceSize() sets error, e.g.
//   "DP-2: grim gave 2048x1280, expected 2560x1600".
// images holds every grab that did succeed.
GrabResult grabOutputs(const QList<Output> &outputs, const GrabOptions &options = {});

// Runs `hyprctl <arguments>` (program from $RILEYSHOT_HYPRCTL when set, else "hyprctl") and returns its stdout, or an
// empty array with *error set when it fails or takes more than 3 s.
QByteArray runHyprctl(const QStringList &arguments, QString *error = nullptr);

} // namespace rileyshot
