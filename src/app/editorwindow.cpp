// SPDX-License-Identifier: GPL-3.0-or-later
#include "editorwindow.h"

#include "deliver.h"
#include "document.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QTimer>

#include <algorithm>
#include <cstdio>

namespace rileyshot {

namespace {

void say(const QString &line)
{
    std::fprintf(stderr, "riley-shot: %s\n", qPrintable(line));
}

// The --report file: one JSON object per line, appended and flushed at once. Does nothing without a path.
class EditorReport
{
public:
    explicit EditorReport(const QString &path)
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

    int exit(int code)
    {
        write({{QStringLiteral("event"), QStringLiteral("exit")}, {QStringLiteral("code"), code}});
        return code;
    }

private:
    QFile m_file;
};

// The toolbar's height plus a margin, for sizing the window around the shot.
constexpr int kChromeHeight = 120;
constexpr int kMargin = 48;

} // namespace

void setUpDocument(Document *document, const QImage &image, const EditorOptions &options)
{
    document->setImage(image);

    const Theme &theme = options.theme;
    const ToolColours &c = options.config.colours;
    auto colour = [&](int tool, const QString &spec) {
        const QColor resolved = theme.resolve(spec);
        if (resolved.isValid())
            document->setToolColour(tool, resolved);
    };
    colour(Document::Pen, c.pen);
    colour(Document::Highlighter, c.highlighter);
    for (int tool : {int(Document::Line), int(Document::Arrow), int(Document::Rectangle), int(Document::Ellipse)})
        colour(tool, c.shape);
    colour(Document::Text, c.text);
    colour(Document::Marker, c.marker);

    document->setFontFamily(theme.font);
    document->setSaveFolder(options.config.resolvedSaveFolder(options.home.isEmpty() ? QDir::homePath()
                                                                                     : options.home));
    document->setFileNamePattern(options.config.fileNamePattern);
    document->setCopyCommand(options.copyCommand.isEmpty() ? copyCommandFromEnvironment() : options.copyCommand);
}

QVariantMap themeMap(const Theme &theme)
{
    QVariantMap map;
    for (const QString &key : Theme::keys())
        map.insert(key, theme.colour(key));
    map.insert(QStringLiteral("font"), theme.font);
    return map;
}

QVariantList paletteList(const Theme &theme)
{
    QVariantList list;
    for (const QColor &c : theme.palette())
        list.append(c);
    return list;
}

int runEditor(const QImage &image, const EditorOptions &options)
{
    EditorReport report(options.reportFile);

    Document document;
    setUpDocument(&document, image, options);

    QQmlEngine engine;
    QQuickView view(&engine, nullptr);
    view.setTitle(QStringLiteral("riley-shot"));
    view.setColor(options.theme.base);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setInitialProperties({{QStringLiteral("document"), QVariant::fromValue(&document)},
                               {QStringLiteral("theme"), themeMap(options.theme)},
                               {QStringLiteral("palette"), paletteList(options.theme)}});
    view.loadFromModule("RileyShot", "Editor");
    if (view.status() != QQuickView::Ready || !view.rootObject()) {
        QStringList lines;
        for (const QQmlError &e : view.errors())
            lines << e.toString();
        say(lines.isEmpty() ? QStringLiteral("Editor.qml did not load") : lines.join(QLatin1Char('\n')));
        return report.exit(4);
    }

    // Big enough for the toolbar, at most 90% of the screen, and no larger than the shot needs at 1:1.
    QSize room(1600, 1000);
    if (QScreen *screen = QGuiApplication::primaryScreen())
        room = screen->availableGeometry().size() * 0.9;
    const QSize want(image.width() + kMargin, image.height() + kChromeHeight + kMargin);
    view.resize(std::clamp(want.width(), std::min(960, room.width()), std::max(room.width(), 320)),
                std::clamp(want.height(), std::min(640, room.height()), std::max(room.height(), 240)));
    view.setMinimumSize(QSize(480, 320));

    QEventLoop loop;
    int result = 0;
    bool done = false;
    auto finish = [&](int code) {
        if (done)
            return;
        done = true;
        result = code;
        loop.exit(code);
    };

    // The window was closed (by the user or the window manager).
    QObject::connect(&view, &QWindow::visibleChanged, &loop, [&](bool visible) {
        if (!visible)
            finish(0);
    });

    // frameSwapped comes from the render thread; handle it on ours.
    bool framed = false;
    QObject::connect(&view, &QQuickWindow::frameSwapped, &loop, [&] {
        if (framed)
            return;
        framed = true;
        const QSize size = document.imageSize();
        report.write({{QStringLiteral("event"), QStringLiteral("editor")},
                      {QStringLiteral("width"), size.width()},
                      {QStringLiteral("height"), size.height()}});
    }, Qt::QueuedConnection);

    QTimer timer;
    timer.setSingleShot(true);
    if (options.exitAfterMs > 0) {
        QObject::connect(&timer, &QTimer::timeout, &loop, [&] { finish(3); });
        timer.start(options.exitAfterMs);
    }

    auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    const bool quitOnLast = app ? app->quitOnLastWindowClosed() : false;
    if (app)
        app->setQuitOnLastWindowClosed(false);

    view.show();
    if (!done)
        loop.exec();
    timer.stop();
    QObject::disconnect(&view, nullptr, &loop, nullptr);
    view.hide();
    if (app)
        app->setQuitOnLastWindowClosed(quitOnLast);
    return report.exit(result);
}

int runEditFile(const CliOptions &options)
{
    Config config;
    {
        QString why;
        if (!Config::load(Config::defaultPath(), &config, &why))
            say(why);
    }
    const Theme theme = Theme::load(Theme::defaultPath());

    QImageReader reader(options.inFile);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        const QString why = QFileInfo::exists(options.inFile) ? reader.errorString() : QStringLiteral("no such file");
        say(QStringLiteral("cannot open %1 as an image: %2").arg(options.inFile, why));
        return EditorReport(options.reportFile).exit(4);
    }

    EditorOptions editor;
    editor.config = config;
    editor.theme = theme;
    editor.home = QDir::homePath();
    editor.exitAfterMs = options.exitAfterMs;
    editor.reportFile = options.reportFile;
    return runEditor(image, editor);
}

} // namespace rileyshot
