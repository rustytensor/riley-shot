// SPDX-License-Identifier: GPL-3.0-or-later
// riley-shot: the command line (src/core/cli.h). M1 has the "grab" command through the capture core; the capture
// modes come with the overlay in M2.
#include "cli.h"
#include "grab.h"
#include "output.h"

#include <QCoreApplication>
#include <QImage>
#include <QStringList>

#include <cstdio>

using namespace rileyshot;

namespace {

const char kUsage[] =
    "Usage:\n"
    "  riley-shot [capture] [--region | --screen | --window] [--no-editor]\n"
    "  riley-shot grab --output NAME --out FILE.png [--pointer]\n"
    "  riley-shot --help\n"
    "\n"
    "  capture       freeze every screen and show the capture bar (the default)\n"
    "  --region      go straight to drawing a region\n"
    "  --screen      go straight to picking a screen\n"
    "  --window      go straight to picking a window\n"
    "  --no-editor   copy and save only; do not open the editor\n"
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
    say(QStringLiteral("the capture overlay is not built yet"));
    return 1;
}
