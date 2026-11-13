// SPDX-License-Identifier: GPL-3.0-or-later
// M3 gate: the editor's window content (builder B: src/qml/Editor.qml and helpers, src/app/documentcanvas.*,
// src/app/editorwindow.*) on a real Document (builder A), offscreen with the software renderer. The QML contract
// is written in src/app/editorwindow.h and src/app/documentcanvas.h; this test loads RileyShot/Editor into a
// 1400x900 window and drives it with the mouse and keyboard like a person would.
//
// Expected values:
//   - Tool buttons: objectName "tool_<name>" sets Document.<Name> (document.h Tool: Select 0 ... Pixelate 12).
//   - Swatches: Theme().palette() = red #e5776a, peach #e9a046, yellow #e3c169, green #9fc184, teal #68bfb5,
//     lavender #a0c0e8, mauve #bf98d9, text #f0ece0 (theme.h).
//   - Fit zoom: min(1, viewport width / output width, viewport height / output height) (documentcanvas.h).
//   - toImage(local) = cropRect.topLeft + local / zoom (documentcanvas.h).
//   - Ctrl+S with save folder <tmp>/shots, pattern "Shot_%Y-%m-%d_%H-%M-%S" and time 2026-11-12 21:05:09 writes
//     <tmp>/shots/Shot_2026-11-12_21-05-09.png (deliver.h fileNameFor's own example, with "Shot").
#include "annotation.h"
#include "config.h"
#include "document.h"
#include "editorwindow.h"
#include "theme.h"

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTime>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>

Q_IMPORT_QML_PLUGIN(RileyShotPlugin)

using namespace rileyshot;

namespace {

QImage white(int w, int h)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(qRgb(255, 255, 255));
    return image;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

struct ToolName
{
    const char *objectName;
    Document::Tool tool;
};

const ToolName kTools[] = {
    {"tool_select", Document::Select},       {"tool_pen", Document::Pen},
    {"tool_highlighter", Document::Highlighter}, {"tool_eraser", Document::Eraser},
    {"tool_line", Document::Line},           {"tool_arrow", Document::Arrow},
    {"tool_rectangle", Document::Rectangle}, {"tool_ellipse", Document::Ellipse},
    {"tool_text", Document::Text},           {"tool_marker", Document::Marker},
    {"tool_crop", Document::Crop},           {"tool_blur", Document::Blur},
    {"tool_pixelate", Document::Pixelate},
};

const char *const kControls[] = {
    "toolbar",      "fillToggle",   "customColour", "strokeWidth",   "undoButton",    "redoButton",
    "copyButton",   "saveButton",   "saveAsButton", "zoomInButton",  "zoomOutButton", "zoomFitButton",
    "viewport",     "canvas",
};

} // namespace

class TestEditorQml : public QObject
{
    Q_OBJECT

    QTemporaryDir *m_tmp = nullptr;
    Document *doc = nullptr;
    QQmlEngine *engine = nullptr;
    QQuickWindow *win = nullptr;
    QQuickItem *editor = nullptr;

    QQuickItem *find(const char *name) const { return editor->findChild<QQuickItem *>(QLatin1String(name)); }

    QPoint centre(QQuickItem *item) const
    {
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    }

    void click(const char *name)
    {
        QQuickItem *item = find(name);
        QVERIFY2(item, name);
        QVERIFY2(item->isVisible(), name);
        const QPoint p = centre(item);
        QVERIFY2(QRect(0, 0, win->width(), win->height()).contains(p),
                 qPrintable(QStringLiteral("%1 is outside the window").arg(QLatin1String(name))));
        QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier, p);
    }

    // QTest::keyClicks takes widgets only: one key click per character.
    void type(const char *text)
    {
        for (const char *c = text; *c; ++c)
            QTest::keyClick(win, *c);
    }

    qreal zoom() const { return find("canvas")->property("zoom").toReal(); }

    // The window point over image point (x,y), with no crop: canvas local (x*zoom, y*zoom).
    QPoint overImage(qreal x, qreal y) const
    {
        return find("canvas")->mapToScene(QPointF(x * zoom(), y * zoom())).toPoint();
    }

    // What the canvas must send for a window point (documentcanvas.h toImage).
    QPointF expectedImagePoint(const QPoint &scene) const
    {
        const QPointF local = find("canvas")->mapFromScene(QPointF(scene));
        return QPointF(doc->cropRect().topLeft()) + local / zoom();
    }

    qreal fitZoom() const
    {
        QQuickItem *viewport = find("viewport");
        const QSize out = doc->outputSize();
        return std::min({1.0, viewport->width() / out.width(), viewport->height() / out.height()});
    }

private slots:
    void init()
    {
        m_tmp = new QTemporaryDir;
        qputenv("FAKE_WLCOPY_DIR", m_tmp->path().toLocal8Bit());
        qunsetenv("FAKE_WLCOPY_EXIT");
        doc = new Document;
        EditorOptions o;
        o.config.saveFolder = m_tmp->filePath(QStringLiteral("shots"));
        o.config.fileNamePattern = QStringLiteral("Shot_%Y-%m-%d_%H-%M-%S");
        o.copyCommand = {QStringLiteral(RILEYSHOT_SOURCE_DIR "/tests/fake/wl-copy"), QStringLiteral("--type"),
                         QStringLiteral("image/png")};
        o.home = m_tmp->path();
        setUpDocument(doc, white(400, 300), o);
        doc->setNow(QDateTime(QDate(2026, 11, 12), QTime(21, 5, 9)));

        engine = new QQmlEngine;
        QQmlComponent component(engine);
        component.loadFromModule("RileyShot", "Editor");
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QObject *object = component.createWithInitialProperties(
            {{QStringLiteral("document"), QVariant::fromValue(doc)},
             {QStringLiteral("theme"), themeMap(Theme())},
             {QStringLiteral("palette"), paletteList(Theme())}});
        QVERIFY2(object, qPrintable(component.errorString()));
        win = new QQuickWindow;
        win->resize(1400, 900);
        editor = qobject_cast<QQuickItem *>(object);
        QVERIFY(editor);
        editor->setParent(win);
        editor->setParentItem(win->contentItem());
        editor->setSize(QSizeF(1400, 900));
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));
        win->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(win));
        QCoreApplication::processEvents();
    }

    void cleanup()
    {
        delete win; // owns the editor item
        win = nullptr;
        editor = nullptr;
        delete engine;
        engine = nullptr;
        delete doc;
        doc = nullptr;
        delete m_tmp;
        m_tmp = nullptr;
    }

    // --- C++ set-up ----------------------------------------------------------------------------------------------
    void setUpFromConfig()
    {
        Document d;
        EditorOptions o;
        o.config.saveFolder = QStringLiteral("~/Shots");
        o.config.fileNamePattern = QStringLiteral("X_%Y");
        o.config.colours.pen = QStringLiteral("#112233");
        o.config.colours.highlighter = QStringLiteral("green");
        o.config.colours.shape = QStringLiteral("teal");
        o.config.colours.text = QStringLiteral("mauve");
        o.config.colours.marker = QStringLiteral("lavender");
        o.home = QStringLiteral("/home/test");
        o.copyCommand = {QStringLiteral("a"), QStringLiteral("b")};
        setUpDocument(&d, white(30, 20), o);
        QCOMPARE(d.imageSize(), QSize(30, 20));
        QCOMPARE(d.toolColour(Document::Pen).rgb(), qRgb(0x11, 0x22, 0x33));
        QCOMPARE(d.toolColour(Document::Highlighter).rgb(), qRgb(0x9f, 0xc1, 0x84));
        for (int t : {int(Document::Line), int(Document::Arrow), int(Document::Rectangle), int(Document::Ellipse)})
            QCOMPARE(d.toolColour(t).rgb(), qRgb(0x68, 0xbf, 0xb5));
        QCOMPARE(d.toolColour(Document::Text).rgb(), qRgb(0xbf, 0x98, 0xd9));
        QCOMPARE(d.toolColour(Document::Marker).rgb(), qRgb(0xa0, 0xc0, 0xe8));
        QCOMPARE(d.fontFamily(), QStringLiteral("IBM Plex Mono"));
        QCOMPARE(d.saveFolder(), QStringLiteral("/home/test/Shots"));
        QCOMPARE(d.fileNamePattern(), QStringLiteral("X_%Y"));
        QCOMPARE(d.copyCommand(), QStringList({QStringLiteral("a"), QStringLiteral("b")}));
        // A colour that does not resolve keeps the document's default (red).
        Document e;
        o.config.colours.pen = QStringLiteral("nonsense");
        setUpDocument(&e, white(30, 20), o);
        QCOMPARE(e.toolColour(Document::Pen).rgb(), qRgb(0xe5, 0x77, 0x6a));

        const QVariantMap theme = themeMap(Theme());
        QCOMPARE(theme.size(), 14); // 13 colours and the font
        QCOMPARE(theme.value(QStringLiteral("accent")).value<QColor>().rgb(), qRgb(0xe9, 0xa0, 0x46));
        QCOMPARE(theme.value(QStringLiteral("font")).toString(), QStringLiteral("IBM Plex Mono"));
        const QVariantList palette = paletteList(Theme());
        QCOMPARE(palette.size(), 8);
        QCOMPARE(palette.first().value<QColor>().rgb(), qRgb(0xe5, 0x77, 0x6a));
        QCOMPARE(palette.last().value<QColor>().rgb(), qRgb(0xf0, 0xec, 0xe0));
    }

    // --- the window's pieces ---------------------------------------------------------------------------------------
    void everythingIsThere()
    {
        for (const ToolName &t : kTools) {
            QQuickItem *item = find(t.objectName);
            QVERIFY2(item && item->isVisible(), t.objectName);
        }
        for (const char *name : kControls) {
            QQuickItem *item = find(name);
            QVERIFY2(item && item->isVisible(), name);
        }
        for (int i = 0; i < 8; ++i) {
            const QByteArray name = "swatch" + QByteArray::number(i);
            QQuickItem *item = find(name.constData());
            QVERIFY2(item && item->isVisible(), name.constData());
        }
        QQuickItem *text = find("textInput");
        QVERIFY(text);
        QVERIFY(!text->isVisible()); // only after the Text tool asks
    }

    void toolButtons()
    {
        for (const ToolName &t : kTools) {
            click(t.objectName);
            QCOMPARE(doc->tool(), int(t.tool));
            QVERIFY2(find(t.objectName)->property("checked").toBool(), t.objectName);
        }
        QVERIFY(!find("tool_select")->property("checked").toBool());
    }

    void swatchesAndFill()
    {
        doc->setTool(Document::Pen);
        const QList<QColor> palette = Theme().palette();
        for (int i = 0; i < 8; ++i) {
            const QByteArray name = "swatch" + QByteArray::number(i);
            click(name.constData());
            QCOMPARE(doc->colour().rgb(), palette[i].rgb());
        }
        QVERIFY(!doc->filled());
        click("fillToggle");
        QVERIFY(doc->filled());
        QVERIFY(find("fillToggle")->property("checked").toBool());
        click("fillToggle");
        QVERIFY(!doc->filled());
        doc->setStrokeWidth(8);
        QCOMPARE(find("strokeWidth")->property("value").toReal(), 8.0);
    }

    // --- fit and zoom ------------------------------------------------------------------------------------------
    void fitAndZoom()
    {
        QQuickItem *canvas = find("canvas");
        QQuickItem *viewport = find("viewport");
        QCOMPARE(canvas->property("availableWidth").toReal(), viewport->width());
        QCOMPARE(canvas->property("availableHeight").toReal(), viewport->height());
        QVERIFY(canvas->property("fit").toBool());
        // 400x300 fits without shrinking: zoom 1 (never enlarged by fitting).
        QCOMPARE(zoom(), fitZoom());
        QCOMPARE(canvas->width(), 400 * zoom());
        QCOMPARE(canvas->height(), 300 * zoom());
        // A DP-2 sized shot is shrunk to fit.
        doc->setImage(white(2560, 1600));
        QTRY_COMPARE(canvas->width(), 2560 * fitZoom());
        const qreal z0 = fitZoom();
        QVERIFY(z0 < 1);
        QCOMPARE(zoom(), z0);
        QCOMPARE(canvas->height(), 1600 * z0);
        click("zoomInButton");
        QVERIFY(qAbs(zoom() - z0 * 1.25) < 1e-9);
        QVERIFY(!canvas->property("fit").toBool());
        click("zoomOutButton");
        QVERIFY(qAbs(zoom() - z0) < 1e-9);
        click("zoomInButton");
        click("zoomFitButton");
        QVERIFY(canvas->property("fit").toBool());
        QVERIFY(qAbs(zoom() - z0) < 1e-9);
        // Ctrl+= zooms in, Ctrl+0 fits.
        QTest::keyClick(win, Qt::Key_Equal, Qt::ControlModifier);
        QVERIFY(qAbs(zoom() - z0 * 1.25) < 1e-9);
        QTest::keyClick(win, Qt::Key_0, Qt::ControlModifier);
        QVERIFY(qAbs(zoom() - z0) < 1e-9);
    }

    void toImage()
    {
        QQuickItem *canvas = find("canvas");
        const qreal z = zoom();
        QPointF p;
        QVERIFY(QMetaObject::invokeMethod(canvas, "toImage", Q_RETURN_ARG(QPointF, p), Q_ARG(QPointF, QPointF(40, 30))));
        QCOMPARE(p, QPointF(40 / z, 30 / z));
        QVERIFY(doc->crop(QRect(20, 10, 200, 150)));
        QVERIFY(QMetaObject::invokeMethod(canvas, "toImage", Q_RETURN_ARG(QPointF, p), Q_ARG(QPointF, QPointF(40, 30))));
        QCOMPARE(p, QPointF(20 + 40 / zoom(), 10 + 30 / zoom()));
    }

    // --- drawing with the mouse ----------------------------------------------------------------------------------
    void drawRectangle()
    {
        click("tool_rectangle");
        const QPoint from = overImage(40, 30), mid = overImage(90, 60), to = overImage(140, 90);
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(win, mid);
        QTest::mouseMove(win, to);
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, to);
        QCOMPARE(doc->annotationCount(), 1);
        const int id = doc->ids().first();
        QCOMPARE(doc->annotation(id).kind, Annotation::Rectangle);
        const QRectF want = QRectF(expectedImagePoint(from), expectedImagePoint(to)).normalized();
        const QRectF got = doc->annotationBounds(id);
        QVERIFY2(qAbs(got.x() - want.x()) < 0.01 && qAbs(got.y() - want.y()) < 0.01
                     && qAbs(got.width() - want.width()) < 0.01 && qAbs(got.height() - want.height()) < 0.01,
                 qPrintable(QStringLiteral("rect %1,%2 %3x%4, want %5,%6 %7x%8")
                                .arg(got.x()).arg(got.y()).arg(got.width()).arg(got.height())
                                .arg(want.x()).arg(want.y()).arg(want.width()).arg(want.height())));
        // And near the intended (40,30) 100x60: within rounding to whole window pixels.
        const qreal slack = 1 / zoom() + 0.01;
        QVERIFY(qAbs(got.x() - 40) <= slack && qAbs(got.y() - 30) <= slack);
        QVERIFY(qAbs(got.width() - 100) <= 2 * slack && qAbs(got.height() - 60) <= 2 * slack);
    }

    void textTool()
    {
        click("tool_text");
        const QPoint at = overImage(30, 40);
        QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier, at);
        QQuickItem *input = find("textInput");
        QTRY_VERIFY(input->isVisible());
        QTRY_VERIFY(input->hasActiveFocus());
        type("Hi");
        QTest::keyClick(win, Qt::Key_Return);
        QCOMPARE(doc->annotationCount(), 1);
        const Annotation a = doc->annotation(doc->ids().first());
        QCOMPARE(a.kind, Annotation::Text);
        QCOMPARE(a.text, QStringLiteral("Hi"));
        const QPointF want = expectedImagePoint(at);
        QVERIFY2(qAbs(a.position.x() - want.x()) < 0.01 && qAbs(a.position.y() - want.y()) < 0.01,
                 qPrintable(QStringLiteral("text at %1,%2").arg(a.position.x()).arg(a.position.y())));
        QTRY_VERIFY(!input->isVisible());
        // Escape cancels a second one.
        QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier, overImage(100, 100));
        QTRY_VERIFY(input->isVisible());
        type("No");
        QTest::keyClick(win, Qt::Key_Escape);
        QTRY_VERIFY(!input->isVisible());
        QCOMPARE(doc->annotationCount(), 1);
    }

    // --- shortcuts and buttons -------------------------------------------------------------------------------------
    void shortcuts()
    {
        const int id = doc->addRectangle(QRectF(40, 30, 100, 60), QColor(0xe5, 0x77, 0x6a), 4, false);
        QTest::keyClick(win, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(doc->annotationCount(), 0);
        QTest::keyClick(win, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(doc->annotationCount(), 1);
        QTest::keyClick(win, Qt::Key_Z, Qt::ControlModifier);
        QTest::keyClick(win, Qt::Key_Y, Qt::ControlModifier);
        QCOMPARE(doc->annotationCount(), 1);
        doc->setTool(Document::Select);
        doc->setSelectedId(id);
        QTest::keyClick(win, Qt::Key_Delete);
        QCOMPARE(doc->annotationCount(), 0);
        QTest::keyClick(win, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(doc->annotationCount(), 1);
        // Ctrl+C: image/png of the render through the (fake) clipboard program.
        QTest::keyClick(win, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(readAll(m_tmp->filePath(QStringLiteral("args"))), QByteArray("--type\nimage/png\n"));
        const QByteArray bytes = readAll(m_tmp->filePath(QStringLiteral("stdin")));
        QVERIFY(bytes.startsWith("\x89PNG\r\n\x1a\n"));
        QCOMPARE(QImage::fromData(bytes, "PNG").convertToFormat(QImage::Format_RGB32), doc->render());
        // Ctrl+S: a PNG in the save folder, named by the pattern.
        QTest::keyClick(win, Qt::Key_S, Qt::ControlModifier);
        const QString saved = m_tmp->filePath(QStringLiteral("shots/Shot_2026-11-12_21-05-09.png"));
        QVERIFY2(QFile::exists(saved), qPrintable(saved));
        QCOMPARE(QImage(saved).convertToFormat(QImage::Format_RGB32), doc->render());
    }

    void buttons()
    {
        QQuickItem *undo = find("undoButton");
        QQuickItem *redo = find("redoButton");
        QVERIFY(!undo->property("enabled").toBool());
        QVERIFY(!redo->property("enabled").toBool());
        doc->addEllipse(QRectF(40, 30, 120, 80), QColor(0xe5, 0x77, 0x6a), 4, false);
        QTRY_VERIFY(undo->property("enabled").toBool());
        click("undoButton");
        QCOMPARE(doc->annotationCount(), 0);
        QTRY_VERIFY(redo->property("enabled").toBool());
        click("redoButton");
        QCOMPARE(doc->annotationCount(), 1);
        click("copyButton");
        QVERIFY(readAll(m_tmp->filePath(QStringLiteral("stdin"))).startsWith("\x89PNG\r\n\x1a\n"));
        click("saveButton");
        QVERIFY(QFile::exists(m_tmp->filePath(QStringLiteral("shots/Shot_2026-11-12_21-05-09.png"))));
    }
};

QTEST_MAIN(TestEditorQml)
#include "tst_editor_qml.moc"
