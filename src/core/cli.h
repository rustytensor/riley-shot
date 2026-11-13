// SPDX-License-Identifier: GPL-3.0-or-later
// riley-shot's command line, parsed without side effects so it can be tested.
//
//   riley-shot [capture] [--region | --screen | --window] [--no-editor]
//   riley-shot grab --output NAME --out FILE.png [--pointer]
//   riley-shot --help
//
// "grab" grabs one output through the capture core (hyprctl monitors -j, then grabOutputs) and writes its device
// pixels to FILE.png; the M1 gate uses it on DP-2. main() exits 2 on Command::Error (after printing error and the
// usage on stderr), 0 on Help, and 0 or 1 for the others.
//
// Owner: builder B (cli.cpp, src/app/main.cpp). The declarations below are the contract the M1 gate tests.
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
    QString error;        // set only with Command::Error: one line
};

// arguments excludes argv[0]. Rules:
//   - an optional first word "capture" or "grab"; no word means capture;
//   - capture takes at most one of --region/--screen/--window, and --no-editor; each at most once;
//   - grab needs --output NAME and --out FILE, and takes --pointer;
//   - --help or -h anywhere gives Help;
//   - anything else (an unknown word or option, two modes, a capture option after grab, a grab option after capture,
//     a missing value) gives Error.
CliOptions parseCli(const QStringList &arguments);

} // namespace rileyshot
