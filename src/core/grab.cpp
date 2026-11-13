// SPDX-License-Identifier: GPL-3.0-or-later
#include "grab.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QProcess>
#include <QTimer>

#include <memory>
#include <vector>

namespace rileyshot {

namespace {

QString sizeText(QSize s)
{
    return QStringLiteral("%1x%2").arg(s.width()).arg(s.height());
}

// The first line of a program's stderr, trimmed, for error messages.
QString firstLine(const QByteArray &bytes)
{
    const QString text = QString::fromUtf8(bytes).trimmed();
    const qsizetype nl = text.indexOf(QLatin1Char('\n'));
    return nl < 0 ? text : text.left(nl).trimmed();
}

// Waits until every process has finished or the deadline passes. With an event loop running the processes' pipes
// are all drained at once, so no grim stalls on a full pipe while another is waited on.
void waitAll(const std::vector<std::unique_ptr<QProcess>> &processes, int timeoutMs)
{
    auto running = [&] {
        for (const auto &p : processes)
            if (p->state() != QProcess::NotRunning)
                return true;
        return false;
    };
    if (!running())
        return;

    if (QCoreApplication::instance()) {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        for (const auto &p : processes) {
            QObject::connect(p.get(), &QProcess::finished, &loop, [&] {
                if (!running())
                    loop.quit();
            });
            QObject::connect(p.get(), &QProcess::errorOccurred, &loop, [&] {
                if (!running())
                    loop.quit();
            });
        }
        timer.start(timeoutMs);
        if (running())
            loop.exec();
        return;
    }

    // No application object: wait on each in turn against one shared deadline.
    QElapsedTimer clock;
    clock.start();
    for (const auto &p : processes) {
        const qint64 left = std::max<qint64>(0, timeoutMs - clock.elapsed());
        p->waitForFinished(static_cast<int>(left));
    }
}

} // namespace

QStringList grimArguments(const QString &output, bool pointer)
{
    QStringList args;
    if (pointer)
        args << QStringLiteral("-c");
    args << QStringLiteral("-t") << QStringLiteral("ppm") << QStringLiteral("-o") << output << QStringLiteral("-");
    return args;
}

GrabResult grabOutputs(const QList<Output> &outputs, const GrabOptions &options)
{
    GrabResult result;
    std::vector<std::unique_ptr<QProcess>> processes;
    processes.reserve(outputs.size());

    // Start them all first, then wait.
    for (const Output &output : outputs) {
        auto p = std::make_unique<QProcess>();
        p->setProgram(options.grim);
        p->setArguments(grimArguments(output.name, options.pointer));
        p->setStandardInputFile(QProcess::nullDevice());
        p->start();
        processes.push_back(std::move(p));
    }

    waitAll(processes, options.timeoutMs);

    QStringList errors;
    for (qsizetype i = 0; i < outputs.size(); ++i) {
        const Output &output = outputs[i];
        QProcess &p = *processes[static_cast<size_t>(i)];
        if (p.state() != QProcess::NotRunning) {
            p.kill();
            p.waitForFinished(1000);
            errors << QStringLiteral("%1: grim took more than %2 ms").arg(output.name).arg(options.timeoutMs);
            continue;
        }
        if (p.error() == QProcess::FailedToStart) {
            errors << QStringLiteral("%1: cannot start %2: %3").arg(output.name, options.grim, p.errorString());
            continue;
        }
        const QByteArray out = p.readAllStandardOutput();
        const QByteArray err = p.readAllStandardError();
        if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
            QString why = p.exitStatus() != QProcess::NormalExit ? QStringLiteral("grim crashed")
                                                                 : QStringLiteral("grim exited %1").arg(p.exitCode());
            const QString detail = firstLine(err);
            if (!detail.isEmpty())
                why += QStringLiteral(" (%1)").arg(detail);
            errors << QStringLiteral("%1: %2").arg(output.name, why);
            continue;
        }
        QImage image;
        if (!image.loadFromData(out, "PPM")) {
            errors << QStringLiteral("%1: grim gave %2 bytes that are not a PPM image").arg(output.name).arg(out.size());
            continue;
        }
        if (image.size() != output.deviceSize()) {
            errors << QStringLiteral("%1: grim gave %2, expected %3")
                          .arg(output.name, sizeText(image.size()), sizeText(output.deviceSize()));
            continue;
        }
        result.images.insert(output.name, image);
    }
    result.error = errors.join(QStringLiteral("; "));
    return result;
}

QByteArray runHyprctl(const QStringList &arguments, QString *error)
{
    const QString program = qEnvironmentVariableIsEmpty("RILEYSHOT_HYPRCTL")
                                ? QStringLiteral("hyprctl")
                                : qEnvironmentVariable("RILEYSHOT_HYPRCTL");
    auto fail = [&](const QString &why) {
        if (error)
            *error = QStringLiteral("hyprctl %1: %2").arg(arguments.join(QLatin1Char(' ')), why);
        return QByteArray();
    };

    QProcess p;
    p.setStandardInputFile(QProcess::nullDevice());
    p.start(program, arguments);
    if (!p.waitForStarted(3000))
        return fail(QStringLiteral("cannot start %1: %2").arg(program, p.errorString()));
    if (!p.waitForFinished(3000)) {
        p.kill();
        p.waitForFinished(1000);
        return fail(QStringLiteral("took more than 3 s"));
    }
    if (p.exitStatus() != QProcess::NormalExit)
        return fail(QStringLiteral("crashed"));
    if (p.exitCode() != 0) {
        QString why = QStringLiteral("exited %1").arg(p.exitCode());
        const QString detail = firstLine(p.readAllStandardError());
        if (!detail.isEmpty())
            why += QStringLiteral(" (%1)").arg(detail);
        return fail(why);
    }
    if (error)
        error->clear();
    return p.readAllStandardOutput();
}

} // namespace rileyshot
