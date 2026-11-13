// SPDX-License-Identifier: GPL-3.0-or-later
// M2 gate: the overlay's QML (builder B: src/qml/*.qml) on a real CaptureController (builder A), offscreen, with
// the software renderer and QT_SCALE_FACTOR=1.25, so the test window has DP-2's devicePixelRatio and its grab is in
// device pixels like DP-2's own.
//
// The QML contract this test drives (RileyShot module, loaded with QQmlComponent::loadFromModule("RileyShot",
// "Overlay")):
//
// Overlay.qml, root Item, one per output surface. Required properties:
//     controller (CaptureController), outputName (string), layoutX, layoutY (int: the output's layoutPos),
//     deviceWidth, deviceHeight (int: Output::deviceSize()), outputScale (real: Output::scale).
//   Its own size: width deviceWidth / outputScale, height deviceHeight / outputScale (the view may resize it to the
//   same). It has focus, and passes every key to controller.keyPressed(event.key, event.modifiers), accepting the
//   event when that returns true. Pointer positions go to controller.pointerPressed/Moved/Released as
//   (layoutX + local x, layoutY + local y, modifiers); moves with and without a button (hover on).
//   Children found by objectName:
//     "frozenImage"     Image at (0,0), source "image://frozen/" + outputName, width deviceWidth / outputScale,
//                       height deviceHeight / outputScale, smooth false, mipmap false, cache false, asynchronous
//                       false. Device pixel for device pixel: this is what the live gate compares.
//     "selectionFrame"  visible only while controller.hasSelection and the selection overlaps this output; its
//                       x, y, width, height are the selection minus (layoutX, layoutY), in the overlay's logical
//                       units (the selection's layout units). Draws the border and the 8 handles.
//     "sizeLabel"       a Text whose text is controller.sizeText, visible with the selection frame.
//     "screenHighlight" visible only in Screen mode while controller.hoverOutput == outputName.
//     "windowHighlight" visible only in Window mode while controller.hoverWindow is not empty and overlaps this
//                       output; x, y, width, height are hoverWindow minus (layoutX, layoutY).
//     "captureBar"      a CaptureBar, visible only when controller.barVisible and controller.barOutput ==
//                       outputName; horizontally centred, near the bottom, wholly inside the overlay. Its geometry
//                       is final on the first frame (no slide or grow animation), and everything it draws, shadow
//                       included, lies inside its rect grown by 16 logical pixels on every side.
//   While there is no selection and nothing is highlighted, the overlay draws nothing but the frozen image outside
//   the capture bar: no dimming, tint, crosshair or label.
//
// CaptureBar.qml, in the Riley theme (controller.theme colours and font), rounded, children by objectName:
//     "modeRegion", "modeScreen", "modeWindow"  buttons with a bool property `checked` (true for controller.mode);
//                                               a click sets controller.mode to CaptureController.Region/Screen/
//                                               Window.
//     "pointerToggle"  `checked` == controller.pointer; a click toggles controller.pointer.
//     "captureButton"  the big round button; `enabled` == controller.canCapture; a click calls controller.capture().
//     "closeButton"    a click calls controller.cancel().
// SelectionFrame.qml and BarButton.qml are builder B's helpers (no test reads them by name).
//
// Expected values: DP-2 is at layout (1440,1847), device 2560x1600, scale 1.25 -> logical 2560/1.25 = 2048 by
// 1600/1.25 = 1280. A local point (lx,ly) is layout (1440+lx, 1847+ly). The frozen DP-2 image is the same one
// tst_controller uses: pixel (x,y) = rgb(x mod 256, y mod 256, (x div 256)*16 + (y div 256)).
#include "capturecontroller.h"
#include "cli.h"
#include "config.h"
#include "output.h"
#include "theme.h"
#include "window.h"

#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <cmath>

Q_IMPORT_QML_PLUGIN(RileyShotPlugin)

using namespace rileyshot;

namespace {

QList<Output> desk()
{
    QFile f(QStringLiteral(RILEYSHOT_TEST_DATA "/monitors.json"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return parseMonitors(f.readAll());
}

QRgb dp2(int x, int y)
{
    return qRgb(x & 0xff, y & 0xff, ((x >> 8) << 4) | (y >> 8));
}

QImage solid(int w, int h, QRgb c)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(c);
    return image;
}

QHash<QString, QImage> grabs()
{
    QImage d(2560, 1600, QImage::Format_RGB32);
    for (int y = 0; y < d.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(d.scanLine(y));
        for (int x = 0; x < d.width(); ++x)
            line[x] = dp2(x, y);
    }
    return {{QStringLiteral("DP-2"), d},
            {QStringLiteral("HDMI-A-1"), solid(1440, 2560, qRgb(0x11, 0x22, 0x33))},
            {QStringLiteral("DP-1"), solid(3440, 1440, qRgb(0x44, 0x55, 0x66))},
            {QStringLiteral("DP-3"), solid(3440, 1440, qRgb(0x77, 0x88, 0x99))},
            {QStringLiteral("HDMI-A-2"), solid(2560, 1600, qRgb(0xaa, 0xbb, 0xcc))}};
}

Client client(const QString &title, const QRect &rect, int workspace, int focus)
{
    Client c;
    c.address = QStringLiteral("0x") + title;
    c.title = title;
    c.rect = rect;
    c.workspace = workspace;
    c.focusHistoryId = focus;
    return c;
}

QList<Client> handMadeClients()
{
    return {client(QStringLiteral("back"), QRect(1500, 1900, 800, 600), 8, 2),
            client(QStringLiteral("front"), QRect(1700, 2007, 400, 300), 8, 0)};
}

} // namespace

class TestOverlayQml : public QObject
{
    Q_OBJECT

    QTemporaryDir *m_tmp = nullptr;
    CaptureController *c = nullptr;
    QQmlEngine *engine = nullptr;
    QQuickWindow *win = nullptr;
    QQuickItem *overlay = nullptr;

    QQuickItem *find(const char *name) const { return overlay->findChild<QQuickItem *>(QLatin1String(name)); }

    QQuickItem *makeOverlay(QQuickWindow *window, const QString &name)
    {
        const QList<Output> all = desk();
        const Output o = all[findOutput(all, name)];
        QQmlComponent component(engine);
        component.loadFromModule("RileyShot", "Overlay");
        if (component.isError()) {
            qWarning("%s", qPrintable(component.errorString()));
            return nullptr;
        }
        QObject *object = component.createWithInitialProperties(
            {{QStringLiteral("controller"), QVariant::fromValue(c)},
             {QStringLiteral("outputName"), o.name},
             {QStringLiteral("layoutX"), o.layoutPos.x()},
             {QStringLiteral("layoutY"), o.layoutPos.y()},
             {QStringLiteral("deviceWidth"), o.deviceSize().width()},
             {QStringLiteral("deviceHeight"), o.deviceSize().height()},
             {QStringLiteral("outputScale"), o.scale}});
        if (!object) {
            qWarning("%s", qPrintable(component.errorString()));
            return nullptr;
        }
        auto *item = qobject_cast<QQuickItem *>(object);
        item->setParent(window);
        item->setParentItem(window->contentItem());
        return item;
    }

    QPoint centre(QQuickItem *item) const
    {
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    }

    void click(const char *name)
    {
        QQuickItem *item = find(name);
        QVERIFY2(item, name);
        QVERIFY2(item->isVisible(), name);
        QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier, centre(item));
    }

    // Local (60,160) -> (460,460) is layout (1500,2007) -> (1900,2307): the selection (1500,2007) 400x300.
    void dragRegion()
    {
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, QPoint(60, 160));
        QTest::mouseMove(win, QPoint(260, 310));
        QTest::mouseMove(win, QPoint(460, 460));
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, QPoint(460, 460));
    }

private slots:
    void init()
    {
        m_tmp = new QTemporaryDir;
        qputenv("FAKE_WLCOPY_DIR", m_tmp->path().toLocal8Bit());
        c = new CaptureController;
        c->setOutputs(desk());
        c->setImages(grabs());
        c->setClients(handMadeClients());
        Config config;
        config.copyOnCapture = false; // nothing leaves the test
        config.saveOnCapture = false;
        c->setConfig(config);
        c->setTheme(Theme());
        c->setStartMode(Mode::Bar);
        c->setCursor(QPointF(1953, 2743)); // on DP-2: the bar goes there
        c->setCopyCommand({QStringLiteral(RILEYSHOT_SOURCE_DIR "/tests/fake/wl-copy")});
        engine = new QQmlEngine;
        c->installImageProvider(engine);
        win = new QQuickWindow;
        win->resize(2048, 1280);
        overlay = makeOverlay(win, QStringLiteral("DP-2"));
        QVERIFY(overlay);
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));
        win->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(win));
        overlay->forceActiveFocus();
        QCOMPARE(win->devicePixelRatio(), 1.25);
    }

    void cleanup()
    {
        delete win; // owns the overlay item
        win = nullptr;
        overlay = nullptr;
        delete engine;
        engine = nullptr;
        delete c;
        c = nullptr;
        delete m_tmp;
    }

    void sizes()
    {
        QCOMPARE(overlay->width(), 2048.0);
        QCOMPARE(overlay->height(), 1280.0);
        QQuickItem *image = find("frozenImage");
        QVERIFY(image);
        QCOMPARE(image->x(), 0.0);
        QCOMPARE(image->y(), 0.0);
        QCOMPARE(image->width(), 2048.0);  // 2560 / 1.25
        QCOMPARE(image->height(), 1280.0); // 1600 / 1.25
        QCOMPARE(image->smooth(), false);
        QCOMPARE(image->property("mipmap").toBool(), false);
        QCOMPARE(image->property("source").toUrl(), QUrl(QStringLiteral("image://frozen/DP-2")));
        QCOMPARE(image->property("status").toInt(), 1); // Image.Ready
        QCOMPARE(image->property("implicitWidth").toReal(), 2560.0); // the provider's image, device size
        QCOMPARE(image->property("implicitHeight").toReal(), 1600.0);
    }

    void nothingDrawnBeforeAChoice()
    {
        for (const char *name : {"selectionFrame", "sizeLabel", "screenHighlight", "windowHighlight"}) {
            QQuickItem *item = find(name);
            QVERIFY2(!item || !item->isVisible(), name);
        }
    }

    // The frozen image lands device pixel for device pixel: the window's grab (2048x1280 at 1.25 = 2560x1600 device
    // pixels) equals the DP-2 grab everywhere outside the bar's rect grown by 16 logical pixels (20 device pixels).
    void pixelsOneToOne()
    {
        QQuickItem *bar = find("captureBar");
        QVERIFY(bar && bar->isVisible());
        const QRectF b = bar->mapRectToScene(QRectF(0, 0, bar->width(), bar->height())).adjusted(-16, -16, 16, 16);
        const QRect skip(QPoint(int(std::floor(b.left() * 1.25)), int(std::floor(b.top() * 1.25))),
                         QPoint(int(std::ceil(b.right() * 1.25)) - 1, int(std::ceil(b.bottom() * 1.25)) - 1));
        const QImage g = win->grabWindow();
        QCOMPARE(g.size(), QSize(2560, 1600));
        int compared = 0, wrong = 0;
        QPoint firstWrong(-1, -1);
        for (int y = 0; y < 1600; ++y)
            for (int x = 0; x < 2560; ++x) {
                if (skip.contains(x, y))
                    continue;
                ++compared;
                if ((g.pixel(x, y) & 0xffffff) != (dp2(x, y) & 0xffffff)) {
                    if (!wrong)
                        firstWrong = QPoint(x, y);
                    ++wrong;
                }
            }
        if (wrong)
            qWarning("%d of %d pixels differ; first at device (%d,%d)", wrong, compared, firstWrong.x(),
                     firstWrong.y());
        QVERIFY(compared > 2560 * 1600 / 2); // the bar must not cover half the screen
        QCOMPARE(wrong, 0);
    }

    void barPlacement()
    {
        QQuickItem *bar = find("captureBar");
        QVERIFY(bar && bar->isVisible());
        const QRectF r = bar->mapRectToScene(QRectF(0, 0, bar->width(), bar->height()));
        QVERIFY(r.width() > 0 && r.height() > 0);
        QVERIFY(r.left() >= 0 && r.top() >= 0 && r.right() <= 2048 && r.bottom() <= 1280);
        QVERIFY(std::abs(r.center().x() - 1024.0) <= 1.0); // centred: 2048 / 2
        QVERIFY(r.center().y() > 640.0);                    // in the lower half
        for (const char *name : {"modeRegion", "modeScreen", "modeWindow", "pointerToggle", "captureButton",
                                 "closeButton"}) {
            QQuickItem *item = find(name);
            QVERIFY2(item && item->isVisible(), name);
            QVERIFY2(r.contains(item->mapRectToScene(QRectF(0, 0, item->width(), item->height()))), name);
        }
    }

    void barOnlyOnItsOutput()
    {
        // HDMI-A-1 is not under the pointer: its overlay has no visible bar.
        QQuickWindow other;
        other.resize(1440, 2560);
        QQuickItem *o = makeOverlay(&other, QStringLiteral("HDMI-A-1"));
        QVERIFY(o);
        other.show();
        QVERIFY(QTest::qWaitForWindowExposed(&other));
        QCOMPARE(o->width(), 1440.0); // device 1440x2560 (turned), scale 1
        QCOMPARE(o->height(), 2560.0);
        QQuickItem *bar = o->findChild<QQuickItem *>(QStringLiteral("captureBar"));
        QVERIFY(!bar || !bar->isVisible());
        // Started with --region: no bar anywhere.
        CaptureController noBar;
        noBar.setOutputs(desk());
        noBar.setStartMode(Mode::Region);
        QVERIFY(!noBar.barVisible());
    }

    void modeButtons()
    {
        QCOMPARE(find("modeRegion")->property("checked").toBool(), true);
        click("modeScreen");
        QCOMPARE(c->mode(), int(CaptureController::Screen));
        QCOMPARE(find("modeScreen")->property("checked").toBool(), true);
        QCOMPARE(find("modeRegion")->property("checked").toBool(), false);
        click("modeWindow");
        QCOMPARE(c->mode(), int(CaptureController::Window));
        QCOMPARE(find("modeWindow")->property("checked").toBool(), true);
        click("modeRegion");
        QCOMPARE(c->mode(), int(CaptureController::Region));
        QCOMPARE(find("modeWindow")->property("checked").toBool(), false);
    }

    void pointerToggle()
    {
        QCOMPARE(find("pointerToggle")->property("checked").toBool(), false);
        click("pointerToggle");
        QCOMPARE(c->pointer(), true);
        QCOMPARE(find("pointerToggle")->property("checked").toBool(), true);
        click("pointerToggle");
        QCOMPARE(c->pointer(), false);
    }

    void regionDrag()
    {
        QCOMPARE(find("captureButton")->isEnabled(), false); // nothing chosen yet
        dragRegion();
        QCOMPARE(c->selection(), QRect(1500, 2007, 400, 300));
        QQuickItem *frame = find("selectionFrame");
        QVERIFY(frame && frame->isVisible());
        // In the overlay's units: (1500-1440, 2007-1847) = (60,160), 400x300.
        const QRectF f = frame->mapRectToItem(overlay, QRectF(0, 0, frame->width(), frame->height()));
        QCOMPARE(f, QRectF(60, 160, 400, 300));
        QQuickItem *label = find("sizeLabel");
        QVERIFY(label && label->isVisible());
        QCOMPARE(label->property("text").toString(), QStringLiteral("500 × 375")); // 400*1.25 x 300*1.25
        QCOMPARE(find("captureButton")->isEnabled(), true);
    }

    void handleDrag()
    {
        dragRegion();
        // The bottom-right handle at local (460,460) dragged by (+40,+20): r 1900+40 = 1940, b 2307+20 = 2327.
        QTest::mousePress(win, Qt::LeftButton, Qt::NoModifier, QPoint(460, 460));
        QTest::mouseMove(win, QPoint(480, 470));
        QTest::mouseMove(win, QPoint(500, 480));
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, QPoint(500, 480));
        QCOMPARE(c->selection(), QRect(1500, 2007, 440, 320));
        QCOMPARE(find("sizeLabel")->property("text").toString(), QStringLiteral("550 × 400")); // 440*1.25, 320*1.25
    }

    void keys()
    {
        dragRegion();
        QTest::keyClick(win, Qt::Key_Right);
        QCOMPARE(c->selection(), QRect(1501, 2007, 400, 300));
        QTest::keyClick(win, Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(c->selection(), QRect(1501, 2007, 400, 301));
        QSignalSpy done(c, &CaptureController::finished);
        QTest::keyClick(win, Qt::Key_Return);
        QCOMPARE(done.count(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), int(CaptureController::Captured));
        QCOMPARE(c->result().size(), QSize(500, 377)); // 400*1.25, ceil(301*1.25 = 376.25)
    }

    void escape()
    {
        QSignalSpy done(c, &CaptureController::finished);
        QTest::keyClick(win, Qt::Key_Escape);
        QCOMPARE(done.count(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), int(CaptureController::Cancelled));
    }

    void captureButton()
    {
        dragRegion();
        QSignalSpy done(c, &CaptureController::finished);
        click("captureButton");
        QCOMPARE(done.count(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), int(CaptureController::Captured));
        QCOMPARE(c->result().size(), QSize(500, 375));
    }

    void closeButton()
    {
        QSignalSpy done(c, &CaptureController::finished);
        click("closeButton");
        QCOMPARE(done.count(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), int(CaptureController::Cancelled));
    }

    void windowHighlight()
    {
        c->setMode(CaptureController::Window);
        // Local (360,253) is layout (1800,2100): "front" (1700,2007) 400x300 is on top there.
        QTest::mouseMove(win, QPoint(360, 253));
        QCOMPARE(c->hoverWindow(), QRect(1700, 2007, 400, 300));
        QQuickItem *h = find("windowHighlight");
        QVERIFY(h && h->isVisible());
        // (1700-1440, 2007-1847) = (260,160).
        QCOMPARE(h->mapRectToItem(overlay, QRectF(0, 0, h->width(), h->height())), QRectF(260, 160, 400, 300));
        // Local (1500,1000) is layout (2940,2847): no window -> no highlight.
        QTest::mouseMove(win, QPoint(1500, 1000));
        QCOMPARE(c->hoverWindow(), QRect());
        QVERIFY(!h->isVisible());
        // A click on "front" picks it.
        QSignalSpy done(c, &CaptureController::finished);
        QTest::mouseMove(win, QPoint(360, 253));
        QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier, QPoint(360, 253));
        QCOMPARE(done.count(), 1);
        QCOMPARE(c->result().size(), QSize(500, 375)); // front on DP-2: 400*1.25 x 300*1.25
    }

    void screenHighlight()
    {
        c->setMode(CaptureController::Screen);
        // Local (1000,500) is layout (2440,2347), on DP-2.
        QTest::mouseMove(win, QPoint(1000, 500));
        QCOMPARE(c->hoverOutput(), QStringLiteral("DP-2"));
        QQuickItem *h = find("screenHighlight");
        QVERIFY(h && h->isVisible());
        QSignalSpy done(c, &CaptureController::finished);
        QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier, QPoint(1000, 500));
        QCOMPARE(done.count(), 1);
        QCOMPARE(c->result().size(), QSize(2560, 1600)); // all of DP-2
    }
};

int main(int argc, char **argv)
{
    // Before the application exists: offscreen, software rendering, and DP-2's scale.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_SCALE_FACTOR", "1.25");
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
    QGuiApplication app(argc, argv);
    TestOverlayQml test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_overlay_qml.moc"
