// SPDX-License-Identifier: GPL-3.0-or-later
// riley-shot's command line, parsed without side effects so it can be tested.
//
//   riley-shot [capture] [--region | --screen | --window] [--no-editor]
//              [--outputs NAME[,NAME...]] [--exit-after-ms N] [--report FILE]      (test aids, from M2)
//   riley-shot grab --output NAME --out FILE.png [--pointer]
//   riley-shot --help
//
// "grab" grabs one output through the capture core (hyprctl monitors -j, then grabOutputs) and writes its device
// pixels to FILE.png; the M1 gate uses it on DP-2. main() exits 2 on Command::Error (after printing error and the
// usage on stderr), 0 on Help, 0 or 1 for Grab, and for Capture the code session.h lists (0 captured, 1 cancelled,
// 3 timer ran out, 4 failure).
//
// Owner: M1 builder B; from M2 builder A (cli.cpp, src/app/main.cpp). The declarations below are the contract the
// M1 and M2 gates test (tests/tst_cli.cpp).
#pragma once

#include <QString>
#include <QStringList>

namespace rileyshot {

enum class Mode { Bar, Region, Screen, Window }; // Bar: show the capture bar; the others skip it

struct CliOptions
{
    enum class Command { Capture, Grab, Help, Error };
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
    QString error;        // set only with Command::Error: one line
};

// arguments excludes argv[0]. Rules:
//   - an optional first word "capture" or "grab"; no word means capture;
//   - capture takes at most one of --region/--screen/--window, and --no-editor; each at most once;
//   - capture also takes --outputs, --exit-after-ms and --report, each at most once and each with a value:
//       --outputs: the value split on ","; an empty value or an empty entry ("DP-2,,DP-1", "DP-2,") is an Error;
//       --exit-after-ms: a decimal integer >= 1 (anything else, 0 included, is an Error); above 10000 it becomes
//         10000, so a test overlay never stays up longer than 10 s;
//       --report: any non-empty path;
//   - grab needs --output NAME and --out FILE, and takes --pointer;
//   - --help or -h anywhere gives Help;
//   - anything else (an unknown word or option, two modes, a capture option after grab, a grab option after capture,
//     a missing value) gives Error.
CliOptions parseCli(const QStringList &arguments);

} // namespace rileyshot
