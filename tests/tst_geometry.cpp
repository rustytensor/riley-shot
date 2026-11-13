// SPDX-License-Identifier: GPL-3.0-or-later
// M1 gate: the pure capture geometry on this desktop's five outputs (builder A: output, region, window, compose).
//
// Input: tests/data/monitors.json, the real `hyprctl monitors -j` of Marc's desktop on 2026-11-12 (serial numbers
// blanked, a disabled sixth output DP-9 added), and tests/data/clients.json, `hyprctl clients -j` in the same shape.
// Every expected value below was worked out by hand from those numbers before any code existed; the arithmetic is in
// the comment above each check.
//
// The outputs (hyprctl: name, mode WxH, layout x,y, scale, transform):
//   HDMI-A-1 2560x1440 at 0,0     scale 1    transform 3 (270 degrees: device 1440x2560, logical 1440x2560)
//            layout x 0..1440, y 0..2560
//   DP-1     3440x1440 at 1440,407 scale 1   -> device = logical 3440x1440, layout x 1440..4880, y 407..1847
//   DP-3     3440x1440 at 4880,407 scale 1   -> device = logical 3440x1440, layout x 4880..8320, y 407..1847
//   DP-2     2560x1600 at 1440,1847 scale 1.25 -> logical 2560/1.25 x 1600/1.25 = 2048x1280,
//            layout x 1440..3488, y 1847..3127
//   HDMI-A-2 2560x1600 at 4495,1847 scale 1.6 -> logical 2560/1.6 x 1600/1.6 = 1600x1000,
//            layout x 4495..6095, y 1847..2847
// DP-1 and DP-2 touch along layout y = 1847 for x 1440..3488. Between DP-2's right edge (3488) and HDMI-A-2's left
// edge (4495) there is no output below y = 1847.
#include "compose.h"
#include "output.h"
#include "region.h"
#include "window.h"

#include <QFile>
#include <QImage>
#include <QtTest>

using namespace rileyshot;

namespace {

QByteArray readData(const char *name)
{
    QFile file(QStringLiteral(RILEYSHOT_TEST_DATA "/") + QLatin1String(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("cannot read test data %s", name);
    return file.readAll();
}

QList<Output> desktop()
{
    QString error;
    const QList<Output> outputs = parseMonitors(readData("monitors.json"), &error);
    if (outputs.size() != 5)
        qFatal("monitors.json: want 5 outputs, got %d (%s)", int(outputs.size()), qPrintable(error));
    return outputs;
}

const Output &named(const QList<Output> &outputs, const char *name)
{
    const int i = findOutput(outputs, QLatin1String(name));
    if (i < 0)
        qFatal("no output %s", name);
    return outputs[i];
}

QImage solid(QSize size, QRgb colour)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(colour);
    return image;
}

// For pieces that are scaled (source size != target size) the smooth filter's fixed-point rounding may move a
// channel of a uniform colour by 1; exact copies are compared exactly.
bool near(QRgb got, QRgb want)
{
    return qAbs(qRed(got) - qRed(want)) <= 1 && qAbs(qGreen(got) - qGreen(want)) <= 1
        && qAbs(qBlue(got) - qBlue(want)) <= 1 && qAlpha(got) == qAlpha(want);
}

QString rgba(QRgb c)
{
    return QStringLiteral("(%1,%2,%3,%4)").arg(qRed(c)).arg(qGreen(c)).arg(qBlue(c)).arg(qAlpha(c));
}

#define CHECK_NEAR(image, x, y, want)                                                                              \
    QVERIFY2(near((image).pixel((x), (y)), (want)),                                                                \
             qPrintable(QStringLiteral("pixel (%1,%2): got %3 want %4")                                           \
                            .arg(x).arg(y).arg(rgba((image).pixel((x), (y)))).arg(rgba(want))))

} // namespace

class TestGeometry : public QObject
{
    Q_OBJECT

private slots:
    // --- hyprctl monitors -j ------------------------------------------------------------------------------------

    void parsesTheFiveOutputs()
    {
        QString error;
        const QList<Output> outputs = parseMonitors(readData("monitors.json"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        // monitors.json lists HDMI-A-1, DP-1, DP-3, DP-2, HDMI-A-2, DP-9; DP-9 has "disabled": true.
        QCOMPARE(outputs.size(), 5);
        QCOMPARE(outputs[0].name, QStringLiteral("HDMI-A-1"));
        QCOMPARE(outputs[1].name, QStringLiteral("DP-1"));
        QCOMPARE(outputs[2].name, QStringLiteral("DP-3"));
        QCOMPARE(outputs[3].name, QStringLiteral("DP-2"));
        QCOMPARE(outputs[4].name, QStringLiteral("HDMI-A-2"));
        QCOMPARE(findOutput(outputs, QStringLiteral("DP-9")), -1);

        const Output &dp2 = outputs[3];
        QCOMPARE(dp2.layoutPos, QPoint(1440, 1847));
        QCOMPARE(dp2.modeSize, QSize(2560, 1600));
        QCOMPARE(dp2.scale, 1.25);
        QCOMPARE(dp2.transform, 0);
        // activeWorkspace.id in monitors.json: HDMI-A-1 7, DP-1 1, DP-3 4, DP-2 8, HDMI-A-2 9.
        QCOMPARE(outputs[0].activeWorkspace, 7);
        QCOMPARE(outputs[1].activeWorkspace, 1);
        QCOMPARE(outputs[2].activeWorkspace, 4);
        QCOMPARE(dp2.activeWorkspace, 8);
        QCOMPARE(outputs[4].activeWorkspace, 9);
        QCOMPARE(outputs[0].transform, 3);
        QCOMPARE(outputs[4].scale, 1.6);
    }

    void rejectsMalformedJson()
    {
        QString error;
        QVERIFY(parseMonitors("{\"not\": \"an array\"}", &error).isEmpty());
        QVERIFY(!error.isEmpty());
        error.clear();
        // An entry without "scale".
        QVERIFY(parseMonitors("[{\"name\":\"X\",\"width\":10,\"height\":10,\"x\":0,\"y\":0,\"transform\":0}]", &error)
                    .isEmpty());
        QVERIFY(!error.isEmpty());
    }

    // --- sizes ------------------------------------------------------------------------------------------------------

    void sizes_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QSize>("device");
        QTest::addColumn<QSize>("logical");
        QTest::addColumn<QRect>("layout");
        // transform 3 turns the 2560x1440 mode by 270 degrees: device = (1440, 2560); scale 1 so logical = device.
        QTest::newRow("HDMI-A-1 rotated") << "HDMI-A-1" << QSize(1440, 2560) << QSize(1440, 2560)
                                          << QRect(0, 0, 1440, 2560);
        QTest::newRow("DP-1") << "DP-1" << QSize(3440, 1440) << QSize(3440, 1440) << QRect(1440, 407, 3440, 1440);
        QTest::newRow("DP-3") << "DP-3" << QSize(3440, 1440) << QSize(3440, 1440) << QRect(4880, 407, 3440, 1440);
        // 2560 / 1.25 = 2048, 1600 / 1.25 = 1280.
        QTest::newRow("DP-2 1.25") << "DP-2" << QSize(2560, 1600) << QSize(2048, 1280) << QRect(1440, 1847, 2048, 1280);
        // 2560 / 1.6 = 1600, 1600 / 1.6 = 1000 (in doubles 1599.9999999999998..., rounded to 1600).
        QTest::newRow("HDMI-A-2 1.6") << "HDMI-A-2" << QSize(2560, 1600) << QSize(1600, 1000)
                                      << QRect(4495, 1847, 1600, 1000);
    }

    void sizes()
    {
        QFETCH(QString, name);
        QFETCH(QSize, device);
        QFETCH(QSize, logical);
        QFETCH(QRect, layout);
        const QList<Output> outputs = desktop();
        const Output &o = named(outputs, qPrintable(name));
        QCOMPARE(o.deviceSize(), device);
        QCOMPARE(o.logicalSize(), logical);
        QCOMPARE(o.layoutRect(), layout);
    }

    // --- layout <-> device, per output -----------------------------------------------------------------------------

    void layoutToDevice_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QPointF>("layout");
        QTest::addColumn<QPointF>("device");
        // device = (layout - layoutPos) * scale.
        // HDMI-A-1 at 0,0 scale 1: the rotation is already in grim's image, so device = layout. Its last pixel is
        // (1439, 2559), not (2559, 1439): the device image is 1440 wide.
        QTest::newRow("HDMI-A-1 origin") << "HDMI-A-1" << QPointF(0, 0) << QPointF(0, 0);
        QTest::newRow("HDMI-A-1 last pixel") << "HDMI-A-1" << QPointF(1439, 2559) << QPointF(1439, 2559);
        QTest::newRow("HDMI-A-1 inside") << "HDMI-A-1" << QPointF(700, 2000) << QPointF(700, 2000);
        // DP-1 at 1440,407: (4879 - 1440, 1846 - 407) = (3439, 1439).
        QTest::newRow("DP-1 origin") << "DP-1" << QPointF(1440, 407) << QPointF(0, 0);
        QTest::newRow("DP-1 last pixel") << "DP-1" << QPointF(4879, 1846) << QPointF(3439, 1439);
        // DP-3 at 4880,407: (6000 - 4880, 1000 - 407) = (1120, 593).
        QTest::newRow("DP-3 origin") << "DP-3" << QPointF(4880, 407) << QPointF(0, 0);
        QTest::newRow("DP-3 inside") << "DP-3" << QPointF(6000, 1000) << QPointF(1120, 593);
        QTest::newRow("DP-3 last pixel") << "DP-3" << QPointF(8319, 1846) << QPointF(3439, 1439);
        // DP-2 at 1440,1847 scale 1.25: (1540 - 1440, 1947 - 1847) * 1.25 = (100, 100) * 1.25 = (125, 125).
        QTest::newRow("DP-2 origin") << "DP-2" << QPointF(1440, 1847) << QPointF(0, 0);
        QTest::newRow("DP-2 inside") << "DP-2" << QPointF(1540, 1947) << QPointF(125, 125);
        // (3487 - 1440, 3126 - 1847) * 1.25 = (2047, 1279) * 1.25 = (2558.75, 1598.75).
        QTest::newRow("DP-2 last logical pixel") << "DP-2" << QPointF(3487, 3126) << QPointF(2558.75, 1598.75);
        // The test pattern's dot: (2720.8 - 1440, 2647.8 - 1847) * 1.25 = (1280.8, 800.8) * 1.25 = (1601, 1001).
        QTest::newRow("DP-2 between logical pixels") << "DP-2" << QPointF(2720.8, 2647.8) << QPointF(1601, 1001);
        // HDMI-A-2 at 4495,1847 scale 1.6: (4995 - 4495, 2097 - 1847) * 1.6 = (500, 250) * 1.6 = (800, 400).
        QTest::newRow("HDMI-A-2 origin") << "HDMI-A-2" << QPointF(4495, 1847) << QPointF(0, 0);
        QTest::newRow("HDMI-A-2 inside") << "HDMI-A-2" << QPointF(4995, 2097) << QPointF(800, 400);
        // (6094 - 4495, 2846 - 1847) * 1.6 = (1599, 999) * 1.6 = (2558.4, 1598.4).
        QTest::newRow("HDMI-A-2 last logical pixel") << "HDMI-A-2" << QPointF(6094, 2846) << QPointF(2558.4, 1598.4);
        // The far corner: (6095 - 4495, 2847 - 1847) * 1.6 = (1600, 1000) * 1.6 = (2560, 1600).
        QTest::newRow("HDMI-A-2 far corner") << "HDMI-A-2" << QPointF(6095, 2847) << QPointF(2560, 1600);
    }

    void layoutToDevice()
    {
        QFETCH(QString, name);
        QFETCH(QPointF, layout);
        QFETCH(QPointF, device);
        const QList<Output> outputs = desktop();
        const Output &o = named(outputs, qPrintable(name));
        // QPointF's == is a fuzzy compare, which absorbs the last-bit error of 2720.8 - 1440 and of * 1.6.
        QCOMPARE(o.layoutToDevice(layout), device);
        // And back: layout = device / scale + layoutPos.
        QCOMPARE(o.deviceToLayout(device), layout);
    }

    void outputAtPoint_data()
    {
        QTest::addColumn<QPointF>("layout");
        QTest::addColumn<QString>("name"); // empty: no output
        // Half-open rectangles: the top-left edge belongs to the output, the bottom-right edge does not.
        QTest::newRow("DP-2 top-left corner") << QPointF(1440, 1847) << "DP-2";
        QTest::newRow("DP-1 last row above DP-2") << QPointF(1440, 1846) << "DP-1";
        QTest::newRow("HDMI-A-1 left of DP-2") << QPointF(1439, 1847) << "HDMI-A-1";
        QTest::newRow("DP-1 last column") << QPointF(4879.5, 407) << "DP-1";
        QTest::newRow("DP-3 first column") << QPointF(4880, 407) << "DP-3";
        QTest::newRow("HDMI-A-1 last pixel") << QPointF(1439, 2559.5) << "HDMI-A-1";
        QTest::newRow("HDMI-A-2 inside") << QPointF(4995, 2097) << "HDMI-A-2";
        // x 3488..4495 below y 1847 is a gap: DP-2 ends at 3488, HDMI-A-2 starts at 4495.
        QTest::newRow("gap between DP-2 and HDMI-A-2") << QPointF(4000, 2000) << "";
        QTest::newRow("HDMI-A-2 right edge") << QPointF(6095, 2000) << "";
        QTest::newRow("below HDMI-A-1") << QPointF(100, 2560) << "";
    }

    void outputAtPoint()
    {
        QFETCH(QPointF, layout);
        QFETCH(QString, name);
        const QList<Output> outputs = desktop();
        const Output *o = outputAt(outputs, layout);
        QCOMPARE(o ? o->name : QString(), name);
    }

    void layoutToDeviceRect_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QRectF>("layout");
        QTest::addColumn<QRect>("device");
        // DP-2: (100, 100) * 1.25 = (125, 125) to (500, 400) * 1.25 = (625, 500).
        QTest::newRow("DP-2 inside") << "DP-2" << QRectF(1540, 1947, 400, 300) << QRect(125, 125, 500, 375);
        // DP-2, running off the bottom-right: offsets (1960, 1253) to (2160, 1353);
        // left 1960 * 1.25 = 2450, top floor(1253 * 1.25 = 1566.25) = 1566,
        // right ceil(2160 * 1.25 = 2700) = 2700 -> clamped to 2560, bottom ceil(1353 * 1.25 = 1691.25) = 1692 -> 1600.
        QTest::newRow("DP-2 clamped") << "DP-2" << QRectF(3400, 3100, 200, 100) << QRect(2450, 1566, 110, 34);
        // HDMI-A-2, a 3x3 at offset (3, 3): floor(3 * 1.6 = 4.8) = 4, ceil(6 * 1.6 = 9.6) = 10 -> 4..10, 6 wide.
        QTest::newRow("HDMI-A-2 fractional") << "HDMI-A-2" << QRectF(4498, 1850, 3, 3) << QRect(4, 4, 6, 6);
        // HDMI-A-2, offset (5, 5) size 5: 5 * 1.6 = 8, 10 * 1.6 = 16 -> 8..16.
        QTest::newRow("HDMI-A-2 whole") << "HDMI-A-2" << QRectF(4500, 1852, 5, 5) << QRect(8, 8, 8, 8);
        // HDMI-A-1 rotated, scale 1: device = layout.
        QTest::newRow("HDMI-A-1") << "HDMI-A-1" << QRectF(100, 2000, 200, 300) << QRect(100, 2000, 200, 300);
        // Entirely outside DP-1: nothing left after clamping.
        QTest::newRow("DP-1 outside") << "DP-1" << QRectF(100, 100, 50, 50) << QRect();
    }

    void layoutToDeviceRect()
    {
        QFETCH(QString, name);
        QFETCH(QRectF, layout);
        QFETCH(QRect, device);
        const QList<Output> outputs = desktop();
        QCOMPARE(named(outputs, qPrintable(name)).layoutToDeviceRect(layout), device);
    }

    // --- region planning -------------------------------------------------------------------------------------------

    void regionInOneScreen()
    {
        // DP-2 only: QRect(1540, 1947, 400, 300) lies inside DP-2 (x 1440..3488, y 1847..3127) and touches no other
        // output (DP-1 ends at y 1847, HDMI-A-1 at x 1440). Scale 1.25: size (400, 300) * 1.25 = (500, 375); the
        // source is the layoutToDeviceRect case above, (125, 125, 500, 375); the target is the whole result.
        const RegionPlan plan = planRegion(desktop(), QRect(1540, 1947, 400, 300));
        QCOMPARE(plan.layout, QRect(1540, 1947, 400, 300));
        QCOMPARE(plan.scale, 1.25);
        QCOMPARE(plan.size, QSize(500, 375));
        QCOMPARE(plan.pieces.size(), 1);
        QCOMPARE(plan.pieces[0], (Piece{QStringLiteral("DP-2"), QRect(125, 125, 500, 375), QRect(0, 0, 500, 375)}));
    }

    void regionInOneScreenAt16()
    {
        // HDMI-A-2 only, scale 1.6: offsets (500, 250), size (100, 50) * 1.6 = (160, 80); source (800, 400, 160, 80).
        const RegionPlan plan = planRegion(desktop(), QRect(4995, 2097, 100, 50));
        QCOMPARE(plan.scale, 1.6);
        QCOMPARE(plan.size, QSize(160, 80));
        QCOMPARE(plan.pieces.size(), 1);
        QCOMPARE(plan.pieces[0], (Piece{QStringLiteral("HDMI-A-2"), QRect(800, 400, 160, 80), QRect(0, 0, 160, 80)}));
    }

    void regionAcrossDp1AndDp2()
    {
        // QRect(1600, 1747, 300, 200): x 1600..1900, y 1747..1947, across the DP-1 / DP-2 edge at y 1847.
        // Join scale = max(1, 1.25) = 1.25; size (300 * 1.25, 200 * 1.25) = (375, 250).
        // DP-1 (first in the list): i = (1600, 1747, 300, 100); device offset (1600 - 1440, 1747 - 407) = (160, 1340),
        //   scale 1 -> source (160, 1340, 300, 100) (its bottom 1440 is DP-1's last row + 1).
        //   target: (0, 0) to (300 * 1.25, 100 * 1.25) = (375, 125) -> (0, 0, 375, 125); scaled up from 300x100.
        // DP-2: i = (1600, 1847, 300, 100); device ((1600 - 1440) * 1.25, 0) = (200, 0), size (375, 125)
        //   -> source (200, 0, 375, 125); target top (1847 - 1747) * 1.25 = 125 -> (0, 125, 375, 125), an exact copy.
        const RegionPlan plan = planRegion(desktop(), QRect(1600, 1747, 300, 200));
        QCOMPARE(plan.scale, 1.25);
        QCOMPARE(plan.size, QSize(375, 250));
        QCOMPARE(plan.pieces.size(), 2);
        QCOMPARE(plan.pieces[0], (Piece{QStringLiteral("DP-1"), QRect(160, 1340, 300, 100), QRect(0, 0, 375, 125)}));
        QCOMPARE(plan.pieces[1], (Piece{QStringLiteral("DP-2"), QRect(200, 0, 375, 125), QRect(0, 125, 375, 125)}));
    }

    void regionAcrossRotatedHdmiA1AndDp1()
    {
        // QRect(1340, 500, 200, 100): x 1340..1540 crosses HDMI-A-1's right edge 1440 into DP-1. Both scale 1.
        // HDMI-A-1: i = (1340, 500, 100, 100) -> source (1340, 500, 100, 100) (inside its 1440x2560 device image)
        //   target (0, 0, 100, 100).
        // DP-1: i = (1440, 500, 100, 100) -> source (0, 500 - 407 = 93, 100, 100), target (100, 0, 100, 100).
        const RegionPlan plan = planRegion(desktop(), QRect(1340, 500, 200, 100));
        QCOMPARE(plan.scale, 1.0);
        QCOMPARE(plan.size, QSize(200, 100));
        QCOMPARE(plan.pieces.size(), 2);
        QCOMPARE(plan.pieces[0],
                 (Piece{QStringLiteral("HDMI-A-1"), QRect(1340, 500, 100, 100), QRect(0, 0, 100, 100)}));
        QCOMPARE(plan.pieces[1], (Piece{QStringLiteral("DP-1"), QRect(0, 93, 100, 100), QRect(100, 0, 100, 100)}));
    }

    void regionAcrossThreeScreens()
    {
        // QRect(1400, 1800, 100, 100): x 1400..1500, y 1800..1900, at the corner where HDMI-A-1 (x < 1440),
        // DP-1 (x >= 1440, y < 1847) and DP-2 (x >= 1440, y >= 1847) meet. S = max(1, 1, 1.25) = 1.25;
        // size (125, 125). Pieces in list order: HDMI-A-1, DP-1, DP-2.
        // HDMI-A-1: i = (1400, 1800, 40, 100) -> source (1400, 1800, 40, 100);
        //   target right ceil(40 * 1.25 = 50) = 50, bottom ceil(100 * 1.25) = 125 -> (0, 0, 50, 125).
        // DP-1: i = (1440, 1800, 60, 47) -> source (0, 1800 - 407 = 1393, 60, 47);
        //   target left floor(40 * 1.25 = 50) = 50, right ceil(100 * 1.25 = 125) = 125,
        //   bottom ceil(47 * 1.25 = 58.75) = 59 -> (50, 0, 75, 59).
        // DP-2: i = (1440, 1847, 60, 53) -> source right ceil(60 * 1.25 = 75) = 75, bottom ceil(53 * 1.25 = 66.25) = 67
        //   -> (0, 0, 75, 67); target left 50, top floor(47 * 1.25 = 58.75) = 58, right 125, bottom 125
        //   -> (50, 58, 75, 67). Row 58 is in both DP-1's and DP-2's target: DP-2, drawn later, wins it.
        const RegionPlan plan = planRegion(desktop(), QRect(1400, 1800, 100, 100));
        QCOMPARE(plan.scale, 1.25);
        QCOMPARE(plan.size, QSize(125, 125));
        QCOMPARE(plan.pieces.size(), 3);
        QCOMPARE(plan.pieces[0],
                 (Piece{QStringLiteral("HDMI-A-1"), QRect(1400, 1800, 40, 100), QRect(0, 0, 50, 125)}));
        QCOMPARE(plan.pieces[1], (Piece{QStringLiteral("DP-1"), QRect(0, 1393, 60, 47), QRect(50, 0, 75, 59)}));
        QCOMPARE(plan.pieces[2], (Piece{QStringLiteral("DP-2"), QRect(0, 0, 75, 67), QRect(50, 58, 75, 67)}));
    }

    void regionOverAGap()
    {
        // QRect(3400, 2000, 1200, 100): x 3400..4600 runs from DP-2 (to 3488) over the gap into HDMI-A-2 (from 4495).
        // S = max(1.25, 1.6) = 1.6; size (1200 * 1.6, 100 * 1.6) = (1920, 160).
        // DP-2: i = (3400, 2000, 88, 100); device offsets (1960, 153) to (2048, 253):
        //   x 1960 * 1.25 = 2450 .. 2048 * 1.25 = 2560; y floor(153 * 1.25 = 191.25) = 191 .. ceil(253 * 1.25 = 316.25)
        //   = 317 -> source (2450, 191, 110, 126); target right ceil(88 * 1.6 = 140.8) = 141 -> (0, 0, 141, 160).
        // HDMI-A-2: i = (4495, 2000, 105, 100); offsets (0, 153) to (105, 253):
        //   x 0 .. 105 * 1.6 = 168; y floor(153 * 1.6 = 244.8) = 244 .. ceil(253 * 1.6 = 404.8) = 405
        //   -> source (0, 244, 168, 161); target left (4495 - 3400) * 1.6 = 1095 * 1.6 = 1752, right 1920
        //   -> (1752, 0, 168, 160).
        // Target x 141..1752 is over no output and belongs to no piece.
        const RegionPlan plan = planRegion(desktop(), QRect(3400, 2000, 1200, 100));
        QCOMPARE(plan.scale, 1.6);
        QCOMPARE(plan.size, QSize(1920, 160));
        QCOMPARE(plan.pieces.size(), 2);
        QCOMPARE(plan.pieces[0], (Piece{QStringLiteral("DP-2"), QRect(2450, 191, 110, 126), QRect(0, 0, 141, 160)}));
        QCOMPARE(plan.pieces[1],
                 (Piece{QStringLiteral("HDMI-A-2"), QRect(0, 244, 168, 161), QRect(1752, 0, 168, 160)}));
    }

    void regionOverNothing()
    {
        // y 3200 is below DP-2 (ends 3127) and x 4000 is in no output's column below y 1847.
        const RegionPlan plan = planRegion(desktop(), QRect(4000, 3200, 10, 10));
        QVERIFY(plan.isEmpty());
        QCOMPARE(plan.size, QSize());
        QCOMPARE(plan.scale, 1.0);
    }

    void wholeScreen()
    {
        // DP-2's own layout rectangle (1440, 1847, 2048, 1280): only DP-2 (DP-1's rectangle ends at y 1847, so the
        // intersection with it is empty). Source = target = the whole 2560x1600 device image.
        const QList<Output> outputs = desktop();
        const RegionPlan plan = planRegion(outputs, QRect(1440, 1847, 2048, 1280));
        QCOMPARE(plan.scale, 1.25);
        QCOMPARE(plan.size, QSize(2560, 1600));
        QCOMPARE(plan.pieces.size(), 1);
        QCOMPARE(plan.pieces[0], (Piece{QStringLiteral("DP-2"), QRect(0, 0, 2560, 1600), QRect(0, 0, 2560, 1600)}));

        const RegionPlan screen = planOutput(named(outputs, "DP-2"));
        QCOMPARE(screen.layout, QRect(1440, 1847, 2048, 1280));
        QCOMPARE(screen.scale, 1.25);
        QCOMPARE(screen.size, QSize(2560, 1600));
        QCOMPARE(screen.pieces, plan.pieces);

        // HDMI-A-1 rotated: the whole screen is its 1440x2560 device image.
        const RegionPlan dell = planOutput(named(outputs, "HDMI-A-1"));
        QCOMPARE(dell.size, QSize(1440, 2560));
        QCOMPARE(dell.pieces.size(), 1);
        QCOMPARE(dell.pieces[0].source, QRect(0, 0, 1440, 2560));
    }

    // --- windows ---------------------------------------------------------------------------------------------------

    void parsesClients()
    {
        QString error;
        const QList<Client> clients = parseClients(readData("clients.json"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        // clients.json holds 6 entries; ...a3 has "mapped": false and ...a4 "hidden": true, so 4 remain in order.
        QCOMPARE(clients.size(), 4);
        QCOMPARE(clients[0].address, QStringLiteral("0x5500000000a1"));
        QCOMPARE(clients[1].address, QStringLiteral("0x5500000000a2"));
        QCOMPARE(clients[2].address, QStringLiteral("0x5500000000a5"));
        QCOMPARE(clients[3].address, QStringLiteral("0x5500000000a6"));
        const Client &a1 = clients[0];
        QCOMPARE(a1.cls, QStringLiteral("org.test.Editor"));
        QCOMPARE(a1.title, QStringLiteral("Notes"));
        QCOMPARE(a1.rect, QRect(1460, 1887, 1000, 600));
        QCOMPARE(a1.monitor, 1);
        QCOMPARE(a1.workspace, 8);
        QCOMPARE(a1.floating, false);
        QCOMPARE(a1.fullscreen, false);
        QCOMPARE(a1.focusHistoryId, 0);
        QCOMPARE(clients[1].floating, true);
        // ...a6 has "fullscreen": 2 (an int mode): fullscreen.
        QCOMPARE(clients[3].fullscreen, true);
        QCOMPARE(clients[3].focusHistoryId, 3);
    }

    void windowToPixels()
    {
        const QList<Output> outputs = desktop();
        const QList<Client> clients = parseClients(readData("clients.json"));
        QCOMPARE(clients.size(), 4);

        // ...a1 on DP-2: at (1460, 1887) size (1000, 600); offsets (20, 40) * 1.25 = (25, 50),
        // size (1000, 600) * 1.25 = (1250, 750).
        RegionPlan plan = planWindow(outputs, clients[0]);
        QCOMPARE(plan.scale, 1.25);
        QCOMPARE(plan.size, QSize(1250, 750));
        QCOMPARE(plan.pieces.size(), 1);
        QCOMPARE(plan.pieces[0], (Piece{QStringLiteral("DP-2"), QRect(25, 50, 1250, 750), QRect(0, 0, 1250, 750)}));

        // ...a2 on HDMI-A-2: at (4515, 1887) size (800, 500); offsets (20, 40) * 1.6 = (32, 64),
        // size (800, 500) * 1.6 = (1280, 800).
        plan = planWindow(outputs, clients[1]);
        QCOMPARE(plan.scale, 1.6);
        QCOMPARE(plan.size, QSize(1280, 800));
        QCOMPARE(plan.pieces.size(), 1);
        QCOMPARE(plan.pieces[0],
                 (Piece{QStringLiteral("HDMI-A-2"), QRect(32, 64, 1280, 800), QRect(0, 0, 1280, 800)}));

        // ...a5 on rotated HDMI-A-1: at (16, 1309) size (1408, 1235), scale 1; x ends at 1424 < 1440, so only
        // HDMI-A-1. Source (16, 1309, 1408, 1235) in the 1440x2560 device image (1309 + 1235 = 2544 <= 2560).
        plan = planWindow(outputs, clients[2]);
        QCOMPARE(plan.scale, 1.0);
        QCOMPARE(plan.size, QSize(1408, 1235));
        QCOMPARE(plan.pieces.size(), 1);
        QCOMPARE(plan.pieces[0],
                 (Piece{QStringLiteral("HDMI-A-1"), QRect(16, 1309, 1408, 1235), QRect(0, 0, 1408, 1235)}));

        // ...a6 floats across DP-1 and DP-2: at (1600, 1747) size (300, 200) = regionAcrossDp1AndDp2.
        plan = planWindow(outputs, clients[3]);
        QCOMPARE(plan.size, QSize(375, 250));
        QCOMPARE(plan.pieces, planRegion(outputs, QRect(1600, 1747, 300, 200)).pieces);
    }

    // --- compose ---------------------------------------------------------------------------------------------------

    void composeExactCopy()
    {
        // DP-2's grab as a gradient, pixel (x, y) = (x % 256, y % 256, (x + y) % 256). regionInOneScreen copies source
        // (125, 125, 500, 375) to (0, 0) unscaled, so result (0, 0) = source (125, 125) = (125, 125, 250) and
        // result (499, 374) = source (624, 499) = (624 - 512, 499 - 256, 1123 - 1024) = (112, 243, 99).
        QImage dp2(2560, 1600, QImage::Format_RGB32);
        for (int y = 0; y < dp2.height(); ++y)
            for (int x = 0; x < dp2.width(); ++x)
                dp2.setPixel(x, y, qRgb(x % 256, y % 256, (x + y) % 256));
        const QImage out = compose(planRegion(desktop(), QRect(1540, 1947, 400, 300)), {{QStringLiteral("DP-2"), dp2}});
        QCOMPARE(out.size(), QSize(500, 375));
        QCOMPARE(out.format(), QImage::Format_ARGB32);
        QCOMPARE(out.pixel(0, 0), qRgba(125, 125, 250, 255));
        QCOMPARE(out.pixel(499, 374), qRgba(112, 243, 99, 255));
        QCOMPARE(out.pixel(1, 0), qRgba(126, 125, 251, 255)); // source (126, 125)
    }

    void composeAcrossScales()
    {
        // regionAcrossDp1AndDp2. DP-1's grab is all (10, 20, 30); DP-2's is all blue with a red pixel at (200, 0)
        // and a green pixel at (574, 124).
        // DP-2's piece is an exact copy of source (200, 0, 375, 125) to (0, 125): source (200, 0) -> result (0, 125)
        // is red, (201, 0) -> (1, 125) blue, (574, 124) -> (574 - 200, 125 + 124) = (374, 249) green, (373, 249) blue.
        // DP-1's piece is scaled 300x100 -> 375x125 from a uniform colour, so all of (0..374, 0..124) is (10, 20, 30).
        QImage dp2 = solid({2560, 1600}, qRgb(0, 0, 255));
        dp2.setPixel(200, 0, qRgb(255, 0, 0));
        dp2.setPixel(574, 124, qRgb(0, 255, 0));
        const QImage out = compose(planRegion(desktop(), QRect(1600, 1747, 300, 200)),
                                   {{QStringLiteral("DP-1"), solid({3440, 1440}, qRgb(10, 20, 30))},
                                    {QStringLiteral("DP-2"), dp2}});
        QCOMPARE(out.size(), QSize(375, 250));
        CHECK_NEAR(out, 0, 0, qRgba(10, 20, 30, 255));
        CHECK_NEAR(out, 374, 124, qRgba(10, 20, 30, 255));
        CHECK_NEAR(out, 187, 62, qRgba(10, 20, 30, 255));
        QCOMPARE(out.pixel(0, 125), qRgba(255, 0, 0, 255));
        QCOMPARE(out.pixel(1, 125), qRgba(0, 0, 255, 255));
        QCOMPARE(out.pixel(0, 126), qRgba(0, 0, 255, 255));
        QCOMPARE(out.pixel(374, 249), qRgba(0, 255, 0, 255));
        QCOMPARE(out.pixel(373, 249), qRgba(0, 0, 255, 255));
    }

    void composeThreeScreensSeam()
    {
        // regionAcrossThreeScreens with uniform grabs: HDMI-A-1 (100, 0, 0), DP-1 (0, 100, 0), DP-2 (0, 0, 100).
        // Targets: HDMI-A-1 (0, 0, 50, 125), DP-1 (50, 0, 75, 59) = rows 0..58, DP-2 (50, 58, 75, 67) = rows 58..124.
        // Row 58 at x >= 50 is DP-2's (drawn last), row 57 DP-1's.
        const QImage out = compose(planRegion(desktop(), QRect(1400, 1800, 100, 100)),
                                   {{QStringLiteral("HDMI-A-1"), solid({1440, 2560}, qRgb(100, 0, 0))},
                                    {QStringLiteral("DP-1"), solid({3440, 1440}, qRgb(0, 100, 0))},
                                    {QStringLiteral("DP-2"), solid({2560, 1600}, qRgb(0, 0, 100))}});
        QCOMPARE(out.size(), QSize(125, 125));
        CHECK_NEAR(out, 0, 0, qRgba(100, 0, 0, 255));
        CHECK_NEAR(out, 49, 124, qRgba(100, 0, 0, 255));
        CHECK_NEAR(out, 50, 0, qRgba(0, 100, 0, 255));
        CHECK_NEAR(out, 124, 57, qRgba(0, 100, 0, 255));
        CHECK_NEAR(out, 124, 58, qRgba(0, 0, 100, 255));
        CHECK_NEAR(out, 50, 124, qRgba(0, 0, 100, 255));
    }

    void composeGapIsTransparent()
    {
        // regionOverAGap: DP-2 all (1, 2, 3) into (0, 0, 141, 160), HDMI-A-2 all (4, 5, 6) into (1752, 0, 168, 160);
        // x 141..1751 is over no output: alpha 0.
        const QImage out = compose(planRegion(desktop(), QRect(3400, 2000, 1200, 100)),
                                   {{QStringLiteral("DP-2"), solid({2560, 1600}, qRgb(1, 2, 3))},
                                    {QStringLiteral("HDMI-A-2"), solid({2560, 1600}, qRgb(4, 5, 6))}});
        QCOMPARE(out.size(), QSize(1920, 160));
        CHECK_NEAR(out, 0, 0, qRgba(1, 2, 3, 255));
        CHECK_NEAR(out, 140, 159, qRgba(1, 2, 3, 255));
        QCOMPARE(qAlpha(out.pixel(141, 0)), 0);
        QCOMPARE(qAlpha(out.pixel(1000, 80)), 0);
        QCOMPARE(qAlpha(out.pixel(1751, 159)), 0);
        CHECK_NEAR(out, 1752, 0, qRgba(4, 5, 6, 255));
        CHECK_NEAR(out, 1919, 159, qRgba(4, 5, 6, 255));
    }

    void composeMissingImageAndEmptyPlan()
    {
        // regionAcrossDp1AndDp2 with only DP-2's grab: DP-1's target (0, 0, 375, 125) stays transparent.
        QImage dp2 = solid({2560, 1600}, qRgb(0, 0, 255));
        dp2.setPixel(200, 0, qRgb(255, 0, 0));
        const QImage out = compose(planRegion(desktop(), QRect(1600, 1747, 300, 200)), {{QStringLiteral("DP-2"), dp2}});
        QCOMPARE(out.size(), QSize(375, 250));
        QCOMPARE(qAlpha(out.pixel(0, 0)), 0);
        QCOMPARE(out.pixel(0, 125), qRgba(255, 0, 0, 255));
        QVERIFY(compose(RegionPlan{}, {}).isNull());
    }
};

QTEST_MAIN(TestGeometry)
#include "tst_geometry.moc"
