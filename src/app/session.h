// SPDX-License-Identifier: GPL-3.0-or-later
// `riley-shot capture`: freeze every screen, show one overlay surface per output, wait for a choice, deliver.
//
// In order:
//   1. hyprctl monitors -j (parseMonitors), limited to CliOptions::outputs when given (an unknown name fails);
//      hyprctl clients -j (parseClients); hyprctl cursorpos -j ({"x": X, "y": Y}) for the bar's output.
//   2. grabOutputs() for every output at once BEFORE any surface exists (and, for the pointer toggle, a second
//      set with GrabOptions::pointer). A failed grab fails the capture (exit 4) without showing anything.
//   3. Config::load(Config::defaultPath()) (a broken file: its error on stderr, then the defaults) and
//      Theme::load(Theme::defaultPath()), into one CaptureController with setStartMode(options.mode).
//   4. One OverlaySurface (overlaysurface.h) per output, all on one QQmlEngine: a QQuickView on that output's
//      QScreen (matched by name), made a LayerShellQt layer surface with scope (namespace) "riley-shot", layer
//      overlay, anchored to all four edges, exclusive zone -1, keyboard interactivity exclusive. Its root is
//      RileyShot/Overlay.qml with the required properties controller, outputName, layoutX, layoutY,
//      deviceWidth, deviceHeight and outputScale (from the Output). Key events from any surface reach the
//      controller.
//   5. CaptureController::finished(result) closes every surface and ends the program with exit code `result`:
//      0 captured and delivered, 1 cancelled (Escape or the close button), 3 the --exit-after-ms timer ran out,
//      4 failure.
//   6. (M3) Without --no-editor, a capture that finished with 0 then opens the editor on controller.result()
//      (editorwindow.h runEditor, with the same Config, Theme and copy command, home QDir::homePath()), after every
//      overlay surface has closed; delivery (copy and save per the config) has already happened in capture().
//      --exit-after-ms also limits the editor: a fresh timer of the same length starts when it opens, and runs out
//      with exit code 3. Otherwise the exit code is runEditor's (0 when its window is closed). The report gets the
//      editor's line ({"event":"editor",...}) before the single exit line.
//
// Test aids (CliOptions):
//   --outputs A,B      only those outputs are grabbed and covered.
//   --exit-after-ms N  CaptureController::expire() after N ms (N is at most 10000).
//   --report FILE      appends one JSON object per line (UTF-8, '\n', flushed at once):
//     {"event":"surface","output":"DP-2","width":2560,"height":1600,"scale":1.25}
//         once per surface, after its first frame is swapped (QQuickWindow::frameSwapped); width and height are
//         qRound(window width * devicePixelRatio) and qRound(window height * devicePixelRatio), scale is the
//         window's devicePixelRatio().
//     {"event":"bar","output":"DP-2","x":X,"y":Y,"width":W,"height":H}
//         right after that output's surface line when the bar is on it: the item with objectName "captureBar"
//         mapped to the window (mapRectToScene of its bounding rect), times devicePixelRatio, rounded outwards to
//         whole device pixels. Device pixels of that output, origin at its top-left.
//     {"event":"ready"}
//         once, after every surface has written its surface line.
//     {"event":"exit","code":N}
//         once, just before the program exits.
//
// Owner: M2 builder A (overlaysurface.h/.cpp); from M3 builder B (session.cpp, main.cpp).
#pragma once

#include "cli.h"

namespace rileyshot {

// Runs the capture command inside an existing QGuiApplication and returns the exit code (see 5. above).
int runCapture(const CliOptions &options);

} // namespace rileyshot
