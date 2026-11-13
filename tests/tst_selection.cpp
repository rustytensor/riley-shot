// SPDX-License-Identifier: GPL-3.0-or-later
// M2 gate: the overlay's logic without UI (builder A: selection.cpp). Every expected value is worked out by hand
// below from the rules in src/core/selection.h and this desktop's outputs (tests/data/monitors.json, which is
// `hyprctl monitors -j` of this desktop):
//
//   output     layout x,y    mode       scale  transform  logical size (device / scale)   layoutRect (l,t)-(r,b)
//   HDMI-A-1   0,0           2560x1440  1      3          1440x2560 (turned)              (0,0)-(1440,2560)
//   DP-1       1440,407      3440x1440  1      0          3440x1440                       (1440,407)-(4880,1847)
//   DP-3       4880,407      3440x1440  1      0          3440x1440                       (4880,407)-(8320,1847)
//   DP-2       1440,1847     2560x1600  1.25   0          2048x1280                       (1440,1847)-(3488,3127)
//   HDMI-A-2   4495,1847     2560x1600  1.6    0          1600x1000                       (4495,1847)-(6095,2847)
//   active workspaces: HDMI-A-1 7, DP-1 1, DP-3 4, DP-2 8, HDMI-A-2 9.
#include "output.h"
#include "selection.h"
#include "window.h"

#include <QFile>
#include <QtTest>

using namespace rileyshot;

Q_DECLARE_METATYPE(rileyshot::Handle)

namespace {

QList<Output> desk()
{
    QFile f(QStringLiteral(RILEYSHOT_TEST_DATA "/monitors.json"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return parseMonitors(f.readAll());
}

// The bounding box of the five layoutRects: left min(0,1440,4880,1440,4495) = 0, top min(0,407,407,1847,1847) = 0,
// right max(1440,4880,8320,3488,6095) = 8320, bottom max(2560,1847,1847,3127,2847) = 3127.
const QRect kDeskBounds(0, 0, 8320, 3127);

// The rectangle most handle cases start from: l=1000 t=2000 r=1400 b=2300 (it lies over HDMI-A-1 and DP-2; for the
// selection only the bounds matter).
const QRect kR(1000, 2000, 400, 300);

RegionSelection withRect(const QRect &r)
{
    RegionSelection s;
    s.setBounds(kDeskBounds);
    s.setRect(r);
    return s;
}

Client client(const QString &title, const QRect &rect, int workspace, int focus)
{
    Client c;
    c.address = QStringLiteral("0x") + title;
    c.cls = QStringLiteral("org.test.") + title;
    c.title = title;
    c.rect = rect;
    c.workspace = workspace;
    c.focusHistoryId = focus;
    return c;
}

// A hand-made client list (indices 0..4):
//   0 back      (1500,1900) 800x600  -> x 1500..2300, y 1900..2500   workspace 8 (DP-2's)     focus 2
//   1 front     (1700,2007) 400x300  -> x 1700..2100, y 2007..2307   workspace 8              focus 0
//   2 elsewhere (1500,1900) 1000x1000-> x 1500..2500, y 1900..2900   workspace 5 (no output's) focus 1
//   3 float     (2000,1000) 500x400  -> x 2000..2500, y 1000..1400   workspace 1 (DP-1's)     focus 3
//   4 across    (1600,1747) 300x200  -> x 1600..1900, y 1747..1947   workspace 1, over the DP-1/DP-2 edge y=1847
QList<Client> handMadeClients()
{
    return {client(QStringLiteral("back"), QRect(1500, 1900, 800, 600), 8, 2),
            client(QStringLiteral("front"), QRect(1700, 2007, 400, 300), 8, 0),
            client(QStringLiteral("elsewhere"), QRect(1500, 1900, 1000, 1000), 5, 1),
            client(QStringLiteral("float"), QRect(2000, 1000, 500, 400), 1, 3),
            client(QStringLiteral("across"), QRect(1600, 1747, 300, 200), 1, 4)};
}

} // namespace

class TestSelection : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { QCOMPARE(desk().size(), 5); }

    void bounds()
    {
        QCOMPARE(layoutBounds(desk()), kDeskBounds);
        QCOMPARE(layoutBounds({}), QRect());
        // DP-2 alone: (1440,1847) and its logical size 2048x1280.
        const QList<Output> all = desk();
        QCOMPARE(layoutBounds({all[findOutput(all, QStringLiteral("DP-2"))]}), QRect(1440, 1847, 2048, 1280));
    }

    void newDrag()
    {
        RegionSelection s;
        s.setBounds(kDeskBounds);
        QVERIFY(s.isEmpty());
        QCOMPARE(s.handleAt(QPoint(100, 100)), Handle::None); // empty: None everywhere
        // P (100,100), Q (300,250): l=100 t=100 r=300 b=250 -> 200x150.
        s.press(QPoint(100, 100));
        QVERIFY(s.dragging());
        QCOMPARE(s.dragHandle(), Handle::New);
        s.move(QPoint(300, 250));
        QCOMPARE(s.rect(), QRect(100, 100, 200, 150));
        s.release(QPoint(300, 250));
        QVERIFY(!s.dragging());
        QCOMPARE(s.dragHandle(), Handle::None);
        QCOMPARE(s.rect(), QRect(100, 100, 200, 150));

        // Dragged up and left: the same rectangle (l = min, r = max).
        RegionSelection back;
        back.setBounds(kDeskBounds);
        back.press(QPoint(300, 250));
        back.release(QPoint(100, 100));
        QCOMPARE(back.rect(), QRect(100, 100, 200, 150));
    }

    void newDragClamped()
    {
        // Q (8400,3200) is clamped to the bounds' right/bottom edges (8320,3127): 8320-8300 = 20, 3127-3100 = 27.
        RegionSelection s;
        s.setBounds(kDeskBounds);
        s.press(QPoint(8300, 3100));
        s.release(QPoint(8400, 3200));
        QCOMPARE(s.rect(), QRect(8300, 3100, 20, 27));
    }

    void clickWithoutDrag()
    {
        RegionSelection s;
        s.setBounds(kDeskBounds);
        s.press(QPoint(500, 500));
        s.release(QPoint(500, 500));
        QVERIFY(s.isEmpty());
        // A press outside an existing rectangle drops it, and a click leaves nothing.
        RegionSelection t = withRect(kR);
        t.press(QPoint(1500, 2150));
        t.release(QPoint(1500, 2150));
        QVERIFY(t.isEmpty());
    }

    void moveAndReleaseWithoutPress()
    {
        RegionSelection s = withRect(kR);
        s.move(QPoint(5, 5));
        s.release(QPoint(6, 6));
        QCOMPARE(s.rect(), kR);
    }

    void handleAt_data()
    {
        QTest::addColumn<QPoint>("point");
        QTest::addColumn<Handle>("handle");
        // kR: l=1000 t=2000 r=1400 b=2300, g = 8.
        QTest::newRow("top-left corner exact") << QPoint(1000, 2000) << Handle::TopLeft;
        QTest::newRow("top-left corner at g") << QPoint(992, 2008) << Handle::TopLeft;       // |992-1000| = 8
        QTest::newRow("top-right corner") << QPoint(1405, 1995) << Handle::TopRight;         // 5 and 5
        QTest::newRow("bottom-right corner") << QPoint(1400, 2300) << Handle::BottomRight;
        QTest::newRow("bottom-left corner") << QPoint(996, 2304) << Handle::BottomLeft;      // 4 and 4
        QTest::newRow("top edge") << QPoint(1200, 2003) << Handle::Top;                      // |2003-2000| = 3
        QTest::newRow("bottom edge") << QPoint(1200, 2297) << Handle::Bottom;                // 3
        QTest::newRow("left edge") << QPoint(1003, 2150) << Handle::Left;
        QTest::newRow("right edge") << QPoint(1398, 2150) << Handle::Right;                  // |1398-1400| = 2
        QTest::newRow("right edge outside") << QPoint(1408, 2150) << Handle::Right;          // 8, still <= g
        QTest::newRow("inside") << QPoint(1200, 2150) << Handle::Move;
        QTest::newRow("inside near corner zone") << QPoint(1009, 2009) << Handle::Move;      // 9 > g both ways
        QTest::newRow("outside right") << QPoint(1500, 2150) << Handle::None;
        QTest::newRow("above, just out of reach") << QPoint(1200, 1991) << Handle::None;     // |1991-2000| = 9
        QTest::newRow("beside the top edge") << QPoint(1410, 1995) << Handle::None;          // x 10 from r
    }

    void handleAt()
    {
        QFETCH(QPoint, point);
        QFETCH(Handle, handle);
        QCOMPARE(withRect(kR).handleAt(point), handle);
    }

    void handleDrag_data()
    {
        QTest::addColumn<QPoint>("press");
        QTest::addColumn<QPoint>("release");
        QTest::addColumn<Handle>("handle");
        QTest::addColumn<QRect>("expected");
        // From kR (l=1000 t=2000 r=1400 b=2300), d = release - press, only the handle's edges move.
        // TopLeft d=(-100,-50): l=900 t=1950 -> (900,1950) 500x350.
        QTest::newRow("top-left") << QPoint(1000, 2000) << QPoint(900, 1950) << Handle::TopLeft
                                  << QRect(900, 1950, 500, 350);
        // Top d=(50,100): t=2100, x ignored -> (1000,2100) 400x200.
        QTest::newRow("top") << QPoint(1200, 2000) << QPoint(1250, 2100) << Handle::Top
                             << QRect(1000, 2100, 400, 200);
        // TopRight d=(100,-100): r=1500 t=1900 -> (1000,1900) 500x400.
        QTest::newRow("top-right") << QPoint(1400, 2000) << QPoint(1500, 1900) << Handle::TopRight
                                   << QRect(1000, 1900, 500, 400);
        // Right d=(50,-150): r=1450, y ignored -> 450x300.
        QTest::newRow("right") << QPoint(1400, 2150) << QPoint(1450, 2000) << Handle::Right
                               << QRect(1000, 2000, 450, 300);
        // BottomRight d=(-50,-50): r=1350 b=2250 -> 350x250.
        QTest::newRow("bottom-right") << QPoint(1400, 2300) << QPoint(1350, 2250) << Handle::BottomRight
                                      << QRect(1000, 2000, 350, 250);
        // Bottom d=(0,100): b=2400 -> 400x400.
        QTest::newRow("bottom") << QPoint(1200, 2300) << QPoint(1200, 2400) << Handle::Bottom
                                << QRect(1000, 2000, 400, 400);
        // BottomLeft d=(100,50): l=1100 b=2350 -> (1100,2000) 300x350.
        QTest::newRow("bottom-left") << QPoint(1000, 2300) << QPoint(1100, 2350) << Handle::BottomLeft
                                     << QRect(1100, 2000, 300, 350);
        // Left d=(-200,0): l=800 -> (800,2000) 600x300.
        QTest::newRow("left") << QPoint(1000, 2150) << QPoint(800, 2150) << Handle::Left
                              << QRect(800, 2000, 600, 300);
        // Left past the right edge, d=(600,0): l=1600 > r=1400 -> swapped: l=1400 r=1600 -> (1400,2000) 200x300.
        QTest::newRow("left flips") << QPoint(1000, 2150) << QPoint(1600, 2150) << Handle::Left
                                    << QRect(1400, 2000, 200, 300);
        // Top past the bottom, d=(0,400): t=2400 > b=2300 -> t=2300 b=2400 -> (1000,2300) 400x100.
        QTest::newRow("top flips") << QPoint(1200, 2000) << QPoint(1200, 2400) << Handle::Top
                                   << QRect(1000, 2300, 400, 100);
        // Move d=(100,-50): (1100,1950), size kept.
        QTest::newRow("move") << QPoint(1200, 2150) << QPoint(1300, 2100) << Handle::Move
                              << QRect(1100, 1950, 400, 300);
        // Move far left, d=(-6200,0): l would be -5200; shifted back so l = bounds left 0 -> (0,2000) 400x300.
        QTest::newRow("move clamped left") << QPoint(1200, 2150) << QPoint(-5000, 2150) << Handle::Move
                                           << QRect(0, 2000, 400, 300);
        // Move far down, d=(0,2000): b would be 4300; shifted so b = 3127 -> t = 3127-300 = 2827.
        QTest::newRow("move clamped bottom") << QPoint(1200, 2150) << QPoint(1200, 4150) << Handle::Move
                                             << QRect(1000, 2827, 400, 300);
        // Right far out, d=(7600,0): r=9000 clamped to 8320 -> width 8320-1000 = 7320.
        QTest::newRow("right clamped") << QPoint(1400, 2150) << QPoint(9000, 2150) << Handle::Right
                                       << QRect(1000, 2000, 7320, 300);
        // TopLeft far out, d=(-2000,-3000): l=-1000 -> 0, t=-1000 -> 0 -> (0,0) 1400x2300.
        QTest::newRow("top-left clamped") << QPoint(1000, 2000) << QPoint(-1000, -1000) << Handle::TopLeft
                                          << QRect(0, 0, 1400, 2300);
        // A press outside (None) starts a new rectangle from (1500,2150) to (1600,2250): 100x100.
        QTest::newRow("outside starts new") << QPoint(1500, 2150) << QPoint(1600, 2250) << Handle::New
                                            << QRect(1500, 2150, 100, 100);
    }

    void handleDrag()
    {
        QFETCH(QPoint, press);
        QFETCH(QPoint, release);
        QFETCH(Handle, handle);
        QFETCH(QRect, expected);
        RegionSelection s = withRect(kR);
        s.press(press);
        QCOMPARE(s.dragHandle(), handle);
        s.release(release);
        QCOMPARE(s.rect(), expected);
    }

    void movesDoNotAddUp()
    {
        // TopLeft: press (1000,2000), move (900,1900), move (950,1950). The second move starts again from kR:
        // d=(-50,-50) -> l=950 t=1950 -> (950,1950) 450x350.
        RegionSelection s = withRect(kR);
        s.press(QPoint(1000, 2000));
        s.move(QPoint(900, 1900));
        QCOMPARE(s.rect(), QRect(900, 1900, 500, 400));
        s.move(QPoint(950, 1950));
        QCOMPARE(s.rect(), QRect(950, 1950, 450, 350));
        s.release(QPoint(950, 1950));
        QCOMPARE(s.rect(), QRect(950, 1950, 450, 350));
    }

    void dragToZeroWidthIsEmpty()
    {
        // Right d=(-400,0): r=1000=l -> width 0 -> empty.
        RegionSelection s = withRect(kR);
        s.press(QPoint(1400, 2150));
        s.release(QPoint(1000, 2150));
        QVERIFY(s.isEmpty());
    }

    void setRectClamped()
    {
        // (8000,3000) 400x300 would end at r=8400 b=3300; shifted to r=8320 b=3127 -> (7920,2827).
        RegionSelection s = withRect(QRect(8000, 3000, 400, 300));
        QCOMPARE(s.rect(), QRect(7920, 2827, 400, 300));
    }

    void keyboard_data()
    {
        QTest::addColumn<QRect>("start");
        QTest::addColumn<bool>("resize");
        QTest::addColumn<int>("dx");
        QTest::addColumn<int>("dy");
        QTest::addColumn<bool>("used");
        QTest::addColumn<QRect>("expected");
        QTest::newRow("nudge right") << kR << false << 1 << 0 << true << QRect(1001, 2000, 400, 300);
        QTest::newRow("nudge left") << kR << false << -1 << 0 << true << QRect(999, 2000, 400, 300);
        QTest::newRow("nudge up") << kR << false << 0 << -1 << true << QRect(1000, 1999, 400, 300);
        QTest::newRow("nudge down") << kR << false << 0 << 1 << true << QRect(1000, 2001, 400, 300);
        QTest::newRow("grow right") << kR << true << 1 << 0 << true << QRect(1000, 2000, 401, 300);
        QTest::newRow("shrink width") << kR << true << -1 << 0 << true << QRect(1000, 2000, 399, 300);
        QTest::newRow("grow down") << kR << true << 0 << 1 << true << QRect(1000, 2000, 400, 301);
        QTest::newRow("shrink height") << kR << true << 0 << -1 << true << QRect(1000, 2000, 400, 299);
        // At the edges of the bounds nothing moves past them.
        QTest::newRow("nudge left at 0") << QRect(0, 2000, 400, 300) << false << -1 << 0 << true
                                         << QRect(0, 2000, 400, 300);
        // r = 7920 + 400 = 8320 = bounds right.
        QTest::newRow("nudge right at edge") << QRect(7920, 2000, 400, 300) << false << 1 << 0 << true
                                             << QRect(7920, 2000, 400, 300);
        QTest::newRow("grow right at edge") << QRect(7920, 2000, 400, 300) << true << 1 << 0 << true
                                            << QRect(7920, 2000, 400, 300);
        // b = 2827 + 300 = 3127 = bounds bottom.
        QTest::newRow("grow down at edge") << QRect(1000, 2827, 400, 300) << true << 0 << 1 << true
                                           << QRect(1000, 2827, 400, 300);
        QTest::newRow("shrink width at 1") << QRect(1000, 2000, 1, 300) << true << -1 << 0 << true
                                           << QRect(1000, 2000, 1, 300);
        QTest::newRow("nothing to nudge") << QRect() << false << 1 << 0 << false << QRect();
        QTest::newRow("nothing to resize") << QRect() << true << 1 << 0 << false << QRect();
    }

    void keyboard()
    {
        QFETCH(QRect, start);
        QFETCH(bool, resize);
        QFETCH(int, dx);
        QFETCH(int, dy);
        QFETCH(bool, used);
        QFETCH(QRect, expected);
        RegionSelection s = withRect(start);
        QCOMPARE(resize ? s.resizeBy(dx, dy) : s.nudge(dx, dy), used);
        QCOMPARE(s.rect().isEmpty() ? QRect() : s.rect(), expected);
    }

    void sizeLabel_data()
    {
        QTest::addColumn<QRect>("rect");
        QTest::addColumn<QString>("text");
        // DP-2 only, scale 1.25: 400*1.25 = 500, 300*1.25 = 375.
        QTest::newRow("on DP-2") << QRect(1500, 2007, 400, 300) << QStringLiteral("500 × 375");
        // HDMI-A-1 only, scale 1: as is.
        QTest::newRow("on HDMI-A-1") << QRect(100, 100, 333, 77) << QStringLiteral("333 × 77");
        // HDMI-A-2 only, scale 1.6: 100*1.6 = 160, 50*1.6 = 80.
        QTest::newRow("on HDMI-A-2") << QRect(4600, 2000, 100, 50) << QStringLiteral("160 × 80");
        // Across DP-1 (y < 1847, scale 1) and DP-2 (y >= 1847, scale 1.25): joined at 1.25 -> 125x125.
        QTest::newRow("across DP-1 and DP-2") << QRect(2000, 1800, 100, 100) << QStringLiteral("125 × 125");
        // In the gap right of DP-2 and left of HDMI-A-2 (x 3488..4495, y >= 1847): no output, no plan.
        QTest::newRow("in a gap") << QRect(3600, 2000, 100, 100) << QString();
        QTest::newRow("empty") << QRect() << QString();
    }

    void sizeLabel()
    {
        QFETCH(QRect, rect);
        QFETCH(QString, text);
        QCOMPARE(sizeText(desk(), rect), text);
    }

    void visible()
    {
        // Workspaces 8 and 1 are active (DP-2, DP-1); 5 is no output's: client 2 drops out.
        const QList<Client> v = visibleClients(handMadeClients(), desk());
        QCOMPARE(v.size(), 4);
        QCOMPARE(v[0].title, QStringLiteral("back"));
        QCOMPARE(v[1].title, QStringLiteral("front"));
        QCOMPARE(v[2].title, QStringLiteral("float"));
        QCOMPARE(v[3].title, QStringLiteral("across"));
    }

    void windowHit_data()
    {
        QTest::addColumn<QPoint>("point");
        QTest::addColumn<int>("index");
        // (1800,2100): in back (focus 2), front (focus 0), elsewhere (inactive) -> front, index 1.
        QTest::newRow("front over back") << QPoint(1800, 2100) << 1;
        // (1600,1950): back only (front starts at x 1700; across ends at y 1947) -> 0.
        QTest::newRow("back only") << QPoint(1600, 1950) << 0;
        // (2200,1200): float only -> 3.
        QTest::newRow("floating on DP-1") << QPoint(2200, 1200) << 3;
        // (1650,1800): across only (back starts at y 1900) -> 4.
        QTest::newRow("across, DP-1 part") << QPoint(1650, 1800) << 4;
        // (1650,1900): back (focus 2) and across (focus 4) -> back, 0.
        QTest::newRow("back over across") << QPoint(1650, 1900) << 0;
        // (2400,2600): only elsewhere covers it, and its workspace is not shown -> -1.
        QTest::newRow("only an inactive workspace") << QPoint(2400, 2600) << -1;
        // Half-open: x 2300 is back's right edge, outside; front ends at 2100 -> -1 (elsewhere inactive).
        QTest::newRow("right edge is outside") << QPoint(2300, 2100) << -1;
        // (1500,1900) is back's top-left corner, inside -> 0.
        QTest::newRow("top-left is inside") << QPoint(1500, 1900) << 0;
    }

    void windowHit()
    {
        QFETCH(QPoint, point);
        QFETCH(int, index);
        QCOMPARE(clientAt(handMadeClients(), desk(), point), index);
    }

    void barOutput()
    {
        const QList<Output> all = desk();
        // (1953,2743): DP-2 spans x 1440..3488, y 1847..3127.
        QCOMPARE(barOutputFor(all, QPointF(1953, 2743)), QStringLiteral("DP-2"));
        QCOMPARE(barOutputFor(all, QPointF(100, 100)), QStringLiteral("HDMI-A-1"));
        // (5000,2000): HDMI-A-2 spans x 4495..6095, y 1847..2847.
        QCOMPARE(barOutputFor(all, QPointF(5000, 2000)), QStringLiteral("HDMI-A-2"));
        // (4000,3000): the gap between DP-2 and HDMI-A-2 -> the first output, HDMI-A-1.
        QCOMPARE(barOutputFor(all, QPointF(4000, 3000)), QStringLiteral("HDMI-A-1"));
        // Limited to DP-2 (--outputs DP-2), the pointer on HDMI-A-1 -> DP-2, the only one.
        QCOMPARE(barOutputFor({all[findOutput(all, QStringLiteral("DP-2"))]}, QPointF(100, 100)),
                 QStringLiteral("DP-2"));
        QCOMPARE(barOutputFor({}, QPointF(0, 0)), QString());
    }
};

QTEST_GUILESS_MAIN(TestSelection)
#include "tst_selection.moc"
