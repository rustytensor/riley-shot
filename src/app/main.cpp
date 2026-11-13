// SPDX-License-Identifier: GPL-3.0-or-later
// riley-shot: the command line (src/core/cli.h): "grab" through the capture core, and capture (the default) through
// the overlay (session.h).
#include "cli.h"
#include "grab.h"
#include "output.h"
#include "session.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QStringList>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdio>

// The RileyShot QML module is compiled into the binary.
Q_IMPORT_QML_PLUGIN(RileyShotPlugin)

using namespace rileyshot;

namespace {

const char kUsage[] =
    "Usage:\n"
    "  riley-shot [capture] [--region | --screen | --window] [--no-editor]\n"
    "             [--outputs NAME[,NAME...]] [--exit-after-ms N] [--report FILE]\n"
    "  riley-shot grab --output NAME --out FILE.png [--pointer]\n"
    "  riley-shot --help\n"
    "\n"
    "  capture       freeze every screen and show the capture bar (the default)\n"
    "  --region      go straight to drawing a region\n"
    "  --screen      go straight to picking a screen\n"
    "  --window      go straight to picking a window\n"
    "  --no-editor   copy and save only; do not open the editor\n"
    "\n"
    "  Test aids:\n"
    "  --outputs NAME[,NAME...]  freeze and cover only these outputs\n"
    "  --exit-after-ms N         give up after N ms (at most 10000), exit code 3\n"
    "  --report FILE             append JSON lines about the surfaces to FILE\n"
    "\n"
    "  Exit codes for capture: 0 captured, 1 cancelled, 3 timer ran out, 4 failure; 2 for a bad command line.\n"
    "\n"
    "  grab          write one output's device pixels to a PNG file\n"
    "  --pointer     include the pointer in the grab\n";

void say(const QString &line)
{
    std::fprintf(stderr, "riley-shot: %s\n", qPrintable(line));
}

int grab(const CliOptions &o)
{
    QString error;
    const QByteArray json = runHyprctl({QStringLiteral("monitors"), QStringLiteral("-j")}, &error);
    if (!error.isEmpty()) {
        say(error);
        return 1;
    }
    const QList<Output> outputs = parseMonitors(json, &error);
    if (outputs.isEmpty()) {
        say(error.isEmpty() ? QStringLiteral("hyprctl lists no outputs") : error);
        return 1;
    }
    const int index = findOutput(outputs, o.output);
    if (index < 0) {
        say(QStringLiteral("no output named %1").arg(o.output));
        return 1;
    }
    GrabOptions options;
    options.pointer = o.pointer;
    const GrabResult result = grabOutputs({outputs[index]}, options);
    if (!result.ok()) {
        say(result.error);
        return 1;
    }
    const QImage image = result.images.value(o.output);
    if (!image.save(o.outFile, "PNG")) {
        say(QStringLiteral("cannot write %1").arg(o.outFile));
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QStringList arguments;
    for (int i = 1; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);
    const CliOptions o = parseCli(arguments);

    // Help and errors never start an application object, let alone a window.
    switch (o.command) {
    case CliOptions::Command::Error:
        say(o.error);
        std::fputs(kUsage, stderr);
        return 2;
    case CliOptions::Command::Help:
        std::fputs(kUsage, stdout);
        return 0;
    case CliOptions::Command::Grab: {
        QCoreApplication app(argc, argv);
        return grab(o);
    }
    case CliOptions::Command::Capture:
        break;
    }
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("riley-shot"));
    QGuiApplication::setDesktopFileName(QStringLiteral("riley-shot"));
    return runCapture(o);
}
