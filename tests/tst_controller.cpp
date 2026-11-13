// SPDX-License-Identifier: GPL-3.0-or-later
// M2 gate: the capture controller (builder A: capturecontroller.cpp), driven the way the overlay's QML drives it,
// offscreen. Expected values are worked out by hand below from src/app/capturecontroller.h, the M1 geometry rules
// (output.h, region.h) and tests/data/monitors.json (the table at the top of tst_selection.cpp).
//
// The frozen grabs are made here, one per output at its device size:
//   DP-2 (2560x1600): pixel (x,y) = rgb(x mod 256, y mod 256, (x div 256) * 16 + (y div 256)); x div 256 <= 9 and
//     y div 256 <= 6, so every pixel is different and its colour says where it came from.
//   HDMI-A-1 (1440x2560) #112233, DP-1 (3440x1440) #445566, DP-3 (3440x1440) #778899, HDMI-A-2 (2560x1600) #aabbcc.
// The real clipboard is never touched: the copy command is tests/fake/wl-copy, which records its arguments and
// stdin in $FAKE_WLCOPY_DIR.
#include "capturecontroller.h"
#include "cli.h"
#include "config.h"
#include "output.h"
#include "theme.h"
#include "window.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

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

// The same hand-made list as tst_selection.cpp ("front" is on top at (1800,2100)).
QList<Client> handMadeClients()
{
    return {client(QStringLiteral("back"), QRect(1500, 1900, 800, 600), 8, 2),
            client(QStringLiteral("front"), QRect(1700, 2007, 400, 300), 8, 0),
            client(QStringLiteral("elsewhere"), QRect(1500, 1900, 1000, 1000), 5, 1),
            client(QStringLiteral("float"), QRect(2000, 1000, 500, 400), 1, 3),
            client(QStringLiteral("across"), QRect(1600, 1747, 300, 200), 1, 4)};
}

bool sameRgb(QRgb a, QRgb b)
{
    return qRed(a) == qRed(b) && qGreen(a) == qGreen(b) && qBlue(a) == qBlue(b);
}

} // namespace

class TestController : public QObject
{
    Q_OBJECT

    QTemporaryDir *m_save = nullptr;
    QTemporaryDir *m_clip = nullptr;
    CaptureController *c = nullptr;

    QString argsFile() const { return m_clip->filePath(QStringLiteral("args")); }
    QString stdinFile() const { return m_clip->filePath(QStringLiteral("stdin")); }

    // A controller set up as session.cpp would: all five outputs, the grabs, the hand-made clients, a config that
    // copies and saves into a scratch folder as "shot_%H-%M-%S", at 21:05:09 -> "shot_21-05-09.png".
    void make(Mode start = Mode::Bar, bool copy = true, bool save = true)
    {
        delete c;
        c = new CaptureController;
        c->setOutputs(desk());
        c->setImages(grabs());
        c->setClients(handMadeClients());
        Config config;
        config.saveFolder = m_save->path();
        config.fileNamePattern = QStringLiteral("shot_%H-%M-%S");
        config.copyOnCapture = copy;
        config.saveOnCapture = save;
        c->setConfig(config);
        c->setTheme(Theme());
        c->setStartMode(start);
        c->setCursor(QPointF(1953, 2743)); // on DP-2
        c->setCopyCommand({QStringLiteral(RILEYSHOT_SOURCE_DIR "/tests/fake/wl-copy"), QStringLiteral("--type"),
                           QStringLiteral("image/png")});
        c->setNow(QDateTime(QDate(2026, 11, 12), QTime(21, 5, 9)));
    }

    void drag(qreal x0, qreal y0, qreal x1, qreal y1)
    {
        c->pointerPressed(x0, y0);
        c->pointerMoved((x0 + x1) / 2, (y0 + y1) / 2);
        c->pointerMoved(x1, y1);
        c->pointerReleased(x1, y1);
    }

private slots:
    void initTestCase() { QCOMPARE(desk().size(), 5); }

    void init()
    {
        m_save = new QTemporaryDir;
        m_clip = new QTemporaryDir;
        QVERIFY(m_save->isValid() && m_clip->isValid());
        qputenv("FAKE_WLCOPY_DIR", m_clip->path().toLocal8Bit());
        qunsetenv("FAKE_WLCOPY_EXIT");
    }

    void cleanup()
    {
        delete c;
        c = nullptr;
        delete m_save;
        delete m_clip;
    }

    void startModes()
    {
        make(Mode::Bar);
        QCOMPARE(c->mode(), int(CaptureController::Region));
        QVERIFY(c->barVisible());
        QCOMPARE(c->barOutput(), QStringLiteral("DP-2")); // (1953,2743) is on DP-2
        make(Mode::Region);
        QCOMPARE(c->mode(), int(CaptureController::Region));
        QVERIFY(!c->barVisible());
        make(Mode::Screen);
        QCOMPARE(c->mode(), int(CaptureController::Screen));
        QVERIFY(!c->barVisible());
        make(Mode::Window);
        QCOMPARE(c->mode(), int(CaptureController::Window));
        c->setMode(7); // out of range: ignored
        QCOMPARE(c->mode(), int(CaptureController::Window));
        c->setMode(CaptureController::Screen);
        QCOMPARE(c->mode(), int(CaptureController::Screen));
    }

    void theme()
    {
        make();
        // Theme's defaults (riley bar/Theme.qml): base #151a23, accent #e9a046, text #f0ece0.
        QCOMPARE(c->theme().value(QStringLiteral("base")).value<QColor>(), QColor(0x15, 0x1a, 0x23));
        QCOMPARE(c->theme().value(QStringLiteral("accent")).value<QColor>(), QColor(0xe9, 0xa0, 0x46));
        QCOMPARE(c->theme().value(QStringLiteral("text")).value<QColor>(), QColor(0xf0, 0xec, 0xe0));
        QCOMPARE(c->theme().value(QStringLiteral("font")).toString(), QStringLiteral("IBM Plex Mono"));
        Theme t;
        t.accent = QColor(0x12, 0x34, 0x56);
        c->setTheme(t);
        QCOMPARE(c->theme().value(QStringLiteral("accent")).value<QColor>(), QColor(0x12, 0x34, 0x56));
        QCOMPARE(c->theme().size(), 14); // 13 colour keys + font
    }

    void pointerDefaultFromConfig()
    {
        make();
        QCOMPARE(c->pointer(), false);
        Config config;
        config.pointer = true;
        c->setConfig(config);
        QCOMPARE(c->pointer(), true);
    }

    void regionDragThenEnter()
    {
        make();
        QSignalSpy done(c, &CaptureController::finished);
        QVERIFY(!c->canCapture());
        // Real positions are floored: (1500.7,2007.9) -> (1500,2007), (1900.2,2307.99) -> (1900,2307).
        // (Rounding would give 1501,2008 / 1900,2308 and fail.)
        drag(1500.7, 2007.9, 1900.2, 2307.99);
        QCOMPARE(c->selection(), QRect(1500, 2007, 400, 300));
        QVERIFY(c->hasSelection());
        QVERIFY(c->canCapture());
        QCOMPARE(c->sizeText(), QStringLiteral("500 × 375")); // 400*1.25, 300*1.25
        QCOMPARE(done.count(), 0); // releasing does not capture
        QVERIFY(c->keyPressed(Qt::Key_Return));
        QCOMPARE(done.count(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), int(CaptureController::Captured));
        QVERIFY(c->isFinished());

        // The region on DP-2 alone: source = layoutToDeviceRect: left (1500-1440)*1.25 = 75, top (2007-1847)*1.25 =
        // 200, right (1900-1440)*1.25 = 575, bottom (2307-1847)*1.25 = 575 -> QRect(75,200, 500x375), the same size
        // as the target (0,0, 500x375), so the pixels are copied exactly: result(i,j) = dp2(75+i, 200+j).
        const QImage r = c->result();
        QCOMPARE(r.size(), QSize(500, 375));
        QVERIFY(sameRgb(r.pixel(0, 0), qRgb(75, 200, 0)));        // dp2(75,200)
        QVERIFY(sameRgb(r.pixel(499, 374), qRgb(62, 62, 34)));    // dp2(574,574): 574-512 = 62, (2<<4)|2 = 34
        QVERIFY(sameRgb(r.pixel(250, 100), qRgb(69, 44, 17)));    // dp2(325,300): 325-256, 300-256, (1<<4)|1
        int wrong = 0;
        for (int j = 0; j < 375; ++j)
            for (int i = 0; i < 500; ++i)
                wrong += !sameRgb(r.pixel(i, j), dp2(75 + i, 200 + j));
        QCOMPARE(wrong, 0);

        // Saved as shot_21-05-09.png in the save folder, the same pixels.
        const QString saved = m_save->filePath(QStringLiteral("shot_21-05-09.png"));
        QCOMPARE(c->savedPath(), saved);
        const QImage file(saved);
        QCOMPARE(file.size(), QSize(500, 375));
        QVERIFY(sameRgb(file.pixel(0, 0), qRgb(75, 200, 0)));
        QVERIFY(sameRgb(file.pixel(499, 374), qRgb(62, 62, 34)));

        // Copied: the fake wl-copy got "--type image/png" and a PNG of the same image on stdin.
        QFile args(argsFile());
        QVERIFY(args.open(QIODevice::ReadOnly));
        QCOMPARE(args.readAll(), QByteArray("--type\nimage/png\n"));
        QFile in(stdinFile());
        QVERIFY(in.open(QIODevice::ReadOnly));
        const QByteArray png = in.readAll();
        QVERIFY(png.startsWith("\x89PNG\r\n\x1a\n"));
        const QImage copied = QImage::fromData(png, "PNG");
        QCOMPARE(copied.size(), QSize(500, 375));
        QVERIFY(sameRgb(copied.pixel(499, 374), qRgb(62, 62, 34)));
    }

    void pointerToggleUsesPointerGrabs()
    {
        // The pointer grabs differ from the plain ones at DP-2 device (75,200) only: magenta there.
        QHash<QString, QImage> withPointer = grabs();
        withPointer[QStringLiteral("DP-2")].setPixel(75, 200, qRgb(255, 0, 255));
        make();
        c->setImages(grabs(), withPointer);
        c->setPointer(true);
        drag(1500, 2007, 1900, 2307);
        c->capture();
        QVERIFY(sameRgb(c->result().pixel(0, 0), qRgb(255, 0, 255)));
        // The overlay always shows the plain grab.
        QVERIFY(sameRgb(c->image(QStringLiteral("DP-2")).pixel(75, 200), qRgb(75, 200, 0)));

        make();
        c->setImages(grabs(), withPointer);
        c->setPointer(false);
        drag(1500, 2007, 1900, 2307);
        c->capture();
        QVERIFY(sameRgb(c->result().pixel(0, 0), qRgb(75, 200, 0)));
    }

    void escapeCancels()
    {
        make();
        QSignalSpy done(c, &CaptureController::finished);
        drag(1500, 2007, 1900, 2307);
        QVERIFY(c->keyPressed(Qt::Key_Escape));
        QCOMPARE(done.count(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), int(CaptureController::Cancelled));
        QCOMPARE(c->savedPath(), QString());
        QVERIFY(QDir(m_save->path()).entryList(QDir::Files).isEmpty());
        QVERIFY(!QFile::exists(argsFile()));
        // Finished once: nothing after that counts.
        c->capture();
        c->cancel();
        c->expire();
        QCOMPARE(done.count(), 1);
    }

    void expireTimesOut()
    {
        make();
        QSignalSpy done(c, &CaptureController::finished);
        c->expire();
        QCOMPARE(done.count(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), int(CaptureController::TimedOut));
    }

    void enterWithoutSelection()
    {
        make();
        QSignalSpy done(c, &CaptureController::finished);
        QVERIFY(c->keyPressed(Qt::Key_Return)); // used, but nothing to capture
        QVERIFY(c->keyPressed(Qt::Key_Enter));
        QCOMPARE(done.count(), 0);
        c->capture();
        QCOMPARE(done.count(), 0);
    }

    void arrowKeys()
    {
        make();
        QVERIFY(!c->keyPressed(Qt::Key_Right)); // no selection yet
        drag(1500, 2007, 1900, 2307);
        const int shift = int(Qt::ShiftModifier);
        QVERIFY(c->keyPressed(Qt::Key_Right));
        QCOMPARE(c->selection(), QRect(1501, 2007, 400, 300));
        QVERIFY(c->keyPressed(Qt::Key_Down));
        QCOMPARE(c->selection(), QRect(1501, 2008, 400, 300));
        QVERIFY(c->keyPressed(Qt::Key_Left));
        QVERIFY(c->keyPressed(Qt::Key_Up));
        QCOMPARE(c->selection(), QRect(1500, 2007, 400, 300));
        QVERIFY(c->keyPressed(Qt::Key_Left, shift));
        QCOMPARE(c->selection(), QRect(1500, 2007, 399, 300));
        QVERIFY(c->keyPressed(Qt::Key_Up, shift));
        QCOMPARE(c->selection(), QRect(1500, 2007, 399, 299));
        QVERIFY(c->keyPressed(Qt::Key_Right, shift));
        QVERIFY(c->keyPressed(Qt::Key_Down, shift));
        QVERIFY(c->keyPressed(Qt::Key_Down, shift));
        QCOMPARE(c->selection(), QRect(1500, 2007, 400, 301));
        // 400x301 layout on DP-2: 500 x ceil(301*1.25 = 376.25) = 377.
        QCOMPARE(c->sizeText(), QStringLiteral("500 × 377"));
        QVERIFY(!c->keyPressed(Qt::Key_A));
        // Arrows are for Region mode only.
        c->setMode(CaptureController::Screen);
        QVERIFY(!c->keyPressed(Qt::Key_Right));
        QCOMPARE(c->selection(), QRect(1500, 2007, 400, 301)); // the region stays across a mode change
    }

    void handlesThroughController()
    {
        make();
        drag(1500, 2007, 1900, 2307);
        QCOMPARE(c->handleAt(1500, 2007), int(Handle::TopLeft));
        QCOMPARE(c->handleAt(1900, 2150), int(Handle::Right));
        QCOMPARE(c->handleAt(1700, 2150), int(Handle::Move));
        QCOMPARE(c->handleAt(100, 100), int(Handle::None));
        // Dragging the bottom-right corner from (1900,2307) by (+100,+93): r=2000 b=2400 -> 500x393.
        drag(1900, 2307, 2000, 2400);
        QCOMPARE(c->selection(), QRect(1500, 2007, 500, 393));
        // A drag inside moves it by (-60,-7).
        drag(1700, 2150, 1640, 2143);
        QCOMPARE(c->selection(), QRect(1440, 2000, 500, 393));
    }

    void screenMode()
    {
        make(Mode::Screen);
        QSignalSpy done(c, &CaptureController::finished);
        // (5000,2000) is on HDMI-A-2 (x 4495..6095, y 1847..2847).
        c->pointerMoved(5000, 2000);
        QCOMPARE(c->hoverOutput(), QStringLiteral("HDMI-A-2"));
        QVERIFY(c->canCapture());
        // Pressed on HDMI-A-2, released on DP-2: no capture.
        c->pointerPressed(5000, 2000);
        c->pointerReleased(2000, 2500);
        QCOMPARE(done.count(), 0);
        // The gap (4000,3000): no output.
        c->pointerMoved(4000, 3000);
        QCOMPARE(c->hoverOutput(), QString());
        // A click on HDMI-A-2 captures all of it: planOutput, 2560x1600 of #aabbcc.
        c->pointerMoved(5000, 2000);
        c->pointerPressed(5000, 2000);
        c->pointerReleased(5001, 2001);
        QCOMPARE(done.count(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), int(CaptureController::Captured));
        QCOMPARE(c->result().size(), QSize(2560, 1600));
        QVERIFY(sameRgb(c->result().pixel(0, 0), qRgb(0xaa, 0xbb, 0xcc)));
        QVERIFY(sameRgb(c->result().pixel(2559, 1599), qRgb(0xaa, 0xbb, 0xcc)));
    }

    void screenModeButtonTakesBarOutput()
    {
        // Nothing hovered: the capture button takes the bar's output, DP-2: its whole grab.
        make(Mode::Bar);
        c->setMode(CaptureController::Screen);
        QVERIFY(c->canCapture());
        c->capture();
        QVERIFY(c->isFinished());
        QCOMPARE(c->result().size(), QSize(2560, 1600));
        QVERIFY(sameRgb(c->result().pixel(0, 0), qRgb(0, 0, 0)));            // dp2(0,0)
        QVERIFY(sameRgb(c->result().pixel(2559, 1599), qRgb(255, 63, 150)));  // 2559 = 9*256+255, 1599 = 6*256+63
    }

    void windowMode()
    {
        make(Mode::Window);
        QSignalSpy done(c, &CaptureController::finished);
        // (2400,2600): only the inactive workspace's window -> nothing.
        c->pointerMoved(2400, 2600);
        QCOMPARE(c->hoverWindow(), QRect());
        QVERIFY(!c->canCapture());
        c->pointerPressed(2400, 2600);
        c->pointerReleased(2400, 2600);
        QCOMPARE(done.count(), 0);
        // (1800,2100): "front" on top of "back".
        c->pointerMoved(1800, 2100);
        QCOMPARE(c->hoverWindow(), QRect(1700, 2007, 400, 300));
        QVERIFY(c->canCapture());
        c->pointerPressed(1800, 2100);
        c->pointerReleased(1800, 2100);
        QCOMPARE(done.count(), 1);
        // front on DP-2: source left (1700-1440)*1.25 = 325, top (2007-1847)*1.25 = 200, right 660*1.25 = 825,
        // bottom 460*1.25 = 575 -> 500x375 copied exactly: result(i,j) = dp2(325+i, 200+j).
        const QImage r = c->result();
        QCOMPARE(r.size(), QSize(500, 375));
        QVERIFY(sameRgb(r.pixel(0, 0), qRgb(69, 200, 16)));    // dp2(325,200): 325-256 = 69, (1<<4)|0
        QVERIFY(sameRgb(r.pixel(499, 374), qRgb(56, 62, 50))); // dp2(824,574): 824-768 = 56, (3<<4)|2 = 50
    }

    void modeChangeClearsHover()
    {
        make(Mode::Window);
        c->pointerMoved(1800, 2100);
        QCOMPARE(c->hoverWindow(), QRect(1700, 2007, 400, 300));
        c->setMode(CaptureController::Region);
        QCOMPARE(c->hoverWindow(), QRect());
        c->setMode(CaptureController::Screen);
        c->pointerMoved(5000, 2000);
        QCOMPARE(c->hoverOutput(), QStringLiteral("HDMI-A-2"));
        c->setMode(CaptureController::Window);
        QCOMPARE(c->hoverOutput(), QString());
    }

    void noCopyNoSave()
    {
        make(Mode::Bar, false, false);
        QSignalSpy done(c, &CaptureController::finished);
        drag(1500, 2007, 1900, 2307);
        c->capture();
        QCOMPARE(done.count(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), int(CaptureController::Captured));
        QVERIFY(!QFile::exists(argsFile()));
        QVERIFY(QDir(m_save->path()).entryList(QDir::Files).isEmpty());
        QCOMPARE(c->savedPath(), QString());
    }

    void copyFailureFails()
    {
        qputenv("FAKE_WLCOPY_EXIT", "1");
        make(Mode::Bar, true, false);
        QSignalSpy done(c, &CaptureController::finished);
        drag(1500, 2007, 1900, 2307);
        c->capture();
        QCOMPARE(done.count(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), int(CaptureController::Failed));
        QVERIFY(!c->error().isEmpty());
    }
};

QTEST_MAIN(TestController)
#include "tst_controller.moc"
