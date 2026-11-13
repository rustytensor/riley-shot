// SPDX-License-Identifier: GPL-3.0-or-later
// riley-shot's command line, parsed without side effects so it can be tested.
//
//   riley-shot [capture] [--region | --screen | --window] [--no-editor]
//              [--outputs NAME[,NAME...]] [--exit-after-ms N] [--report FILE]      (test aids, from M2)
//   riley-shot grab --output NAME --out FILE.png [--pointer]
//   riley-shot edit FILE [--exit-after-ms N] [--report FILE]                        (M3)
//   riley-shot --help
//
// "edit" opens FILE (any image Qt reads) in the editor (src/app/editorwindow.h runEditFile): exit 0 when its window
// is closed, 3 when the --exit-after-ms timer ran out, 4 when FILE cannot be read.
// "grab" grabs one output through the capture core (hyprctl monitors -j, then grabOutputs) and writes its device
// pixels to FILE.png; the M1 gate uses it on DP-2. main() exits 2 on Command::Error (after printing error and the
// usage on stderr), 0 on Help, 0 or 1 for Grab, and for Capture the code session.h lists (0 captured, 1 cancelled,
// 3 timer ran out, 4 failure).
//
// Owner: M1 builder B; M2 builder A; from M3 builder B (cli.cpp, src/app/main.cpp). The declarations below are the
// contract the M1, M2 and M3 gates test (tests/tst_cli.cpp, tests/tst_editor_cli.cpp).
#pragma once

#include <QString>
#include <QStringList>

namespace rileyshot {

enum class Mode { Bar, Region, Screen, Window }; // Bar: show the capture bar; the others skip it

struct CliOptions
{
    enum class Command { Capture, Grab, Edit, Help, Error };
    Command command = Command::Capture;
    Mode mode = Mode::Bar;
    bool editor = true;   // false with --no-editor
    QString output;       // grab --output
    QString outFile;      // grab --out
    bool pointer = false; // grab --pointer
    // Capture test aids (M2), so the live gate can limit the overlay to one output and have it close by itself.
    QStringList outputs;  // --outputs DP-2,HDMI-A-2: only these outputs; empty means all
    int exitAfterMs = 0;  // --exit-after-ms N: give up after N ms (exit 3); 0 means no timer
    QString reportFile;   // --report FILE: JSON lines about the surfaces (session.h)
    QString inFile;       // edit FILE (M3)
    QString error;        // set only with Command::Error: one line
};

// arguments excludes argv[0]. Rules:
//   - an optional first word "capture", "grab" or "edit"; no word means capture;
//   - capture takes at most one of --region/--screen/--window, and --no-editor; each at most once;
//   - capture also takes --outputs, --exit-after-ms and --report, each at most once and each with a value:
//       --outputs: the value split on ","; an empty value or an empty entry ("DP-2,,DP-1", "DP-2,") is an Error;
//       --exit-after-ms: a decimal integer >= 1 (anything else, 0 included, is an Error); above 10000 it becomes
//         10000, so a test overlay never stays up longer than 10 s;
//       --report: any non-empty path;
//   - grab needs --output NAME and --out FILE, and takes --pointer;
//   - edit needs exactly one FILE (a non-empty word not starting with "-", anywhere after "edit") and takes
//     --exit-after-ms and --report with the capture rules above (each at most once); any other option after edit
//     (--region, --screen, --window, --no-editor, --outputs, a grab option) is an Error;
//   - --help or -h anywhere gives Help;
//   - anything else (an unknown word or option, two modes, a capture option after grab, a grab option after capture,
//     a missing value, "edit" anywhere but first) gives Error.
CliOptions parseCli(const QStringList &arguments);

} // namespace rileyshot
