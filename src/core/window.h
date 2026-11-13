// SPDX-License-Identifier: GPL-3.0-or-later
// Windows as `hyprctl clients -j` reports them, and a window's rectangle as a region plan. Pure: no I/O.
//
// Owner: builder A (window.cpp). The declarations below are the contract the M1 gate tests; do not change them.
#pragma once

#include "region.h"

#include <QByteArray>
#include <QList>
#include <QRect>
#include <QString>

namespace rileyshot {

struct Client
{
    QString address;    // "address", e.g. "0x55d0c3a1b2c0"
    QString cls;        // "class"
    QString title;      // "title"
    QRect rect;         // QRect("at"[0], "at"[1], "size"[0], "size"[1]), layout coordinates
    int monitor = -1;   // "monitor": the output's hyprctl "id"
    int workspace = 0;  // "workspace"."id"
    bool floating = false;   // "floating"
    bool fullscreen = false; // "fullscreen" != 0 (Hyprland gives an int mode; older versions a bool)
    int focusHistoryId = -1; // "focusHistoryID": 0 is the focused window
    bool operator==(const Client &) const = default;
};

// Parses `hyprctl clients -j`, in JSON order, leaving out entries with "mapped": false or "hidden": true.
// Malformed JSON gives an empty list and, when error is given, a one-line reason in *error.
QList<Client> parseClients(const QByteArray &json, QString *error = nullptr);

// The window's pixels: planRegion(outputs, client.rect).
RegionPlan planWindow(const QList<Output> &outputs, const Client &client);

} // namespace rileyshot
