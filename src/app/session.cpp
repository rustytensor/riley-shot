// SPDX-License-Identifier: GPL-3.0-or-later
#include "session.h"

#include "capturecontroller.h"
#include "config.h"
#include "deliver.h"
#include "editorwindow.h"
#include "grab.h"
#include "output.h"
#include "overlaysurface.h"
#include "theme.h"
#include "window.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QScreen>
#include <QTimer>

#include <cstdio>
#include <memory>
#include <vector>

namespace rileyshot {

namespace {

void say(const QString &line)
{
    std::fprintf(stderr, "riley-shot: %s\n", qPrintable(line));
}

// --report FILE: one JSON object per line, flushed at once. Does nothing without a file.
class Report
{
public:
    explicit Report(const QString &path)
    {
        if (path.isEmpty())
            return;
        m_file.setFileName(path);
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append))
            say(QStringLiteral("cannot write the report %1").arg(path));
    }

    void write(const QJsonObject &object)
    {
        if (!m_file.isOpen())
            return;
        m_file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
        m_file.write("\n");
        m_file.flush();
    }

    // Lets another writer (the editor) append to the same file after this.
    void close() { m_file.close(); }

    int exit(int code)
    {
        write({{QStringLiteral("event"), QStringLiteral("exit")}, {QStringLiteral("code"), code}});
        return code;
    }

private:
    QFile m_file;
};

// hyprctl cursorpos -j: {"x": X, "y": Y}. False when it cannot be read.
bool cursorPosition(QPointF *pos)
{
    QString error;
    const QByteArray json = runHyprctl({QStringLiteral("cursorpos"), QStringLiteral("-j")}, &error);
    if (!error.isEmpty())
        return false;
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    if (!o.contains(QStringLiteral("x")) || !o.contains(QStringLiteral("y")))
        return false;
    *pos = QPointF(o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble());
    return true;
}

QScreen *screenNamed(const QString &name)
{
    for (QScreen *s : QGuiApplication::screens())
        if (s->name() == name)
            return s;
    return nullptr;
}

} // namespace

int runCapture(const CliOptions &options)
{
    Report report(options.reportFile);
    const int failed = int(CaptureController::Failed);

    // 1. What is on screen.
    QString error;
    const QByteArray monitorsJson = runHyprctl({QStringLiteral("monitors"), QStringLiteral("-j")}, &error);
    if (!error.isEmpty()) {
        say(error);
        return report.exit(failed);
    }
    QList<Output> all = parseMonitors(monitorsJson, &error);
    if (all.isEmpty()) {
        say(error.isEmpty() ? QStringLiteral("hyprctl lists no outputs") : error);
        return report.exit(failed);
    }
    QList<Output> outputs;
    if (options.outputs.isEmpty()) {
        outputs = all;
    } else {
        for (const QString &name : options.outputs)
            if (findOutput(all, name) < 0) {
                say(QStringLiteral("no output named %1").arg(name));
                return report.exit(failed);
            }
        for (const Output &o : all)
            if (options.outputs.contains(o.name))
                outputs.append(o);
    }

    QList<Client> clients;
    {
        QString why;
        const QByteArray json = runHyprctl({QStringLiteral("clients"), QStringLiteral("-j")}, &why);
        if (why.isEmpty())
            clients = parseClients(json, &why);
        if (!why.isEmpty())
            say(QStringLiteral("no window list (%1); Window mode will find nothing").arg(why));
    }
    QPointF cursor = QPointF(outputs.first().layoutPos);
    if (!cursorPosition(&cursor))
        say(QStringLiteral("no pointer position from hyprctl; the bar goes on %1").arg(outputs.first().name));

    // 2. Freeze every output before any surface exists, and the same with the pointer drawn in.
    const GrabResult plain = grabOutputs(outputs);
    if (!plain.ok()) {
        say(plain.error);
        return report.exit(failed);
    }
    GrabOptions withPointerOptions;
    withPointerOptions.pointer = true;
    const GrabResult withPointer = grabOutputs(outputs, withPointerOptions);
    if (!withPointer.ok()) {
        say(withPointer.error);
        return report.exit(failed);
    }

    // 3. Configuration and theme.
    Config config;
    {
        QString why;
        if (!Config::load(Config::defaultPath(), &config, &why))
            say(why);
    }
    const Theme theme = Theme::load(Theme::defaultPath());

    // Every screen needs its QScreen before anything shows.
    QList<QScreen *> screens;
    for (const Output &o : outputs) {
        QScreen *s = screenNamed(o.name);
        if (!s) {
            say(QStringLiteral("Qt has no screen named %1").arg(o.name));
            return report.exit(failed);
        }
        screens.append(s);
    }

    CaptureController controller;
    controller.setOutputs(outputs);
    controller.setImages(plain.images, withPointer.images);
    controller.setClients(clients);
    controller.setConfig(config);
    controller.setTheme(theme);
    controller.setStartMode(options.mode);
    controller.setCursor(cursor);
    controller.setCopyCommand(copyCommandFromEnvironment());

    auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (app)
        app->setQuitOnLastWindowClosed(false);

    // 4. One surface per output, all on one engine.
    QQmlEngine engine;
    controller.installImageProvider(&engine);
    std::vector<std::unique_ptr<OverlaySurface>> surfaces;
    for (qsizetype i = 0; i < outputs.size(); ++i) {
        auto surface = std::make_unique<OverlaySurface>(&engine, &controller, outputs[i], screens[i]);
        if (!surface->isValid()) {
            say(surface->errorString());
            return report.exit(failed);
        }
        surfaces.push_back(std::move(surface));
    }

    QEventLoop loop;
    int result = failed;
    bool done = false;
    QObject::connect(&controller, &CaptureController::finished, &loop, [&](int code) {
        if (done)
            return;
        done = true;
        result = code;
        loop.exit(code);
    });

    qsizetype framed = 0;
    for (const auto &surface : surfaces) {
        OverlaySurface *s = surface.get();
        QObject::connect(s, &OverlaySurface::firstFrame, &loop, [&, s] {
            const QSize size = s->deviceSize();
            report.write({{QStringLiteral("event"), QStringLiteral("surface")},
                          {QStringLiteral("output"), s->outputName()},
                          {QStringLiteral("width"), size.width()},
                          {QStringLiteral("height"), size.height()},
                          {QStringLiteral("scale"), s->devicePixelRatio()}});
            if (s->hasVisibleBar()) {
                const QRect bar = s->barDeviceRect();
                report.write({{QStringLiteral("event"), QStringLiteral("bar")},
                              {QStringLiteral("output"), s->outputName()},
                              {QStringLiteral("x"), bar.x()},
                              {QStringLiteral("y"), bar.y()},
                              {QStringLiteral("width"), bar.width()},
                              {QStringLiteral("height"), bar.height()}});
            }
            if (++framed == qsizetype(surfaces.size()))
                report.write({{QStringLiteral("event"), QStringLiteral("ready")}});
        });
    }

    if (options.exitAfterMs > 0)
        QTimer::singleShot(options.exitAfterMs, &controller, &CaptureController::expire);

    for (const auto &surface : surfaces)
        surface->show();

    if (!done)
        loop.exec();

    // 5. Close every surface, then leave with the result.
    for (const auto &surface : surfaces)
        surface->hide();
    surfaces.clear();
    if (!controller.error().isEmpty())
        say(controller.error());
    else if (!controller.savedPath().isEmpty())
        std::printf("%s\n", qPrintable(controller.savedPath()));
    std::fflush(stdout);

    // 6. The editor, on the captured image, once every overlay has gone. It writes the report's editor line and
    // the single exit line itself.
    if (result == int(CaptureController::Captured) && options.editor && !controller.result().isNull()) {
        EditorOptions editor;
        editor.config = config;
        editor.theme = theme;
        editor.copyCommand = copyCommandFromEnvironment();
        editor.home = QDir::homePath();
        editor.exitAfterMs = options.exitAfterMs;
        editor.reportFile = options.reportFile;
        report.close();
        return runEditor(controller.result(), editor);
    }
    return report.exit(result);
}

} // namespace rileyshot
