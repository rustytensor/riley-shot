// SPDX-License-Identifier: GPL-3.0-or-later
// M1 gate: grabbing outputs (builder B: grab.cpp), against tests/fake/grim, which logs its arguments and gives each
// output a uniform colour at the output's device size (see the fake for the table). Real grim on DP-2 is the gate's
// live part (tests/run-capture.sh), not this test.
#include "grab.h"
#include "output.h"

#include <QElapsedTimer>
#include <QFile>
#include <QtTest>

#include <limits>

using namespace rileyshot;

namespace {

const QString kFakeGrim = QStringLiteral(RILEYSHOT_SOURCE_DIR "/tests/fake/grim");

QList<Output> desktop()
{
    QFile file(QStringLiteral(RILEYSHOT_TEST_DATA "/monitors.json"));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("cannot read monitors.json");
    return parseMonitors(file.readAll());
}

QList<Output> pick(const QStringList &names)
{
    const QList<Output> all = desktop();
    QList<Output> out;
    for (const QString &n : names)
        out.append(all[findOutput(all, n)]);
    return out;
}

QStringList logLines(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

} // namespace

class TestGrab : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_log;

private slots:
    void init()
    {
        m_log = m_dir.filePath(QStringLiteral("grim.log"));
        QFile::remove(m_log);
        qputenv("FAKE_GRIM_LOG", m_log.toLocal8Bit());
        qunsetenv("FAKE_GRIM_DELAY");
        qunsetenv("FAKE_GRIM_LOGICAL");
        qunsetenv("FAKE_GRIM_FAIL");
    }

    void arguments()
    {
        QCOMPARE(grimArguments(QStringLiteral("DP-2"), false),
                 (QStringList{"-t", "ppm", "-o", "DP-2", "-"}));
        QCOMPARE(grimArguments(QStringLiteral("HDMI-A-1"), true),
                 (QStringList{"-c", "-t", "ppm", "-o", "HDMI-A-1", "-"}));
    }

    void grabsInParallelAtDeviceSize()
    {
        // Three outputs, each fake grim sleeping 1 s: one after the other takes at least 3 s; all at once about 1 s
        // plus start-up. The bound 2.5 s separates the two.
        qputenv("FAKE_GRIM_DELAY", "1");
        GrabOptions options;
        options.grim = kFakeGrim;
        QElapsedTimer timer;
        timer.start();
        const GrabResult result = grabOutputs(pick({"DP-1", "DP-2", "HDMI-A-1"}), options);
        const qint64 elapsed = timer.elapsed();
        QVERIFY2(result.ok(), qPrintable(result.error));
        QVERIFY2(elapsed < 2500, qPrintable(QStringLiteral("took %1 ms: not in parallel").arg(elapsed)));
        QCOMPARE(result.images.size(), 3);
        // Device sizes: DP-1 3440x1440, DP-2 2560x1600, HDMI-A-1 rotated 1440x2560.
        QCOMPARE(result.images.value("DP-1").size(), QSize(3440, 1440));
        QCOMPARE(result.images.value("DP-2").size(), QSize(2560, 1600));
        QCOMPARE(result.images.value("HDMI-A-1").size(), QSize(1440, 2560));
        // The fake's colours: DP-1 (0, 22, 0), DP-2 (44, 44, 0), HDMI-A-1 (11, 0, 0).
        QCOMPARE(QColor(result.images.value("DP-1").pixel(100, 100)), QColor(0, 22, 0));
        QCOMPARE(QColor(result.images.value("DP-2").pixel(2559, 1599)), QColor(44, 44, 0));
        QCOMPARE(QColor(result.images.value("HDMI-A-1").pixel(1439, 2559)), QColor(11, 0, 0));
        // No pointer asked: grim never got -c, and (0, 0) is the output's colour.
        QCOMPARE(QColor(result.images.value("DP-2").pixel(0, 0)), QColor(44, 44, 0));

        QStringList lines = logLines(m_log);
        QCOMPARE(lines.size(), 3);
        QStringList args;
        qint64 first = std::numeric_limits<qint64>::max(), last = 0;
        for (const QString &line : lines) {
            const qint64 at = line.section(QLatin1Char(' '), 0, 0).toLongLong();
            first = std::min(first, at);
            last = std::max(last, at);
            args.append(line.section(QLatin1Char(' '), 1));
        }
        args.sort();
        QCOMPARE(args, (QStringList{"-t ppm -o DP-1 -", "-t ppm -o DP-2 -", "-t ppm -o HDMI-A-1 -"}));
        // All three started before any could finish its 1 s sleep.
        QVERIFY2(last - first < 900, qPrintable(QStringLiteral("starts %1 ms apart").arg(last - first)));
    }

    void pointer()
    {
        GrabOptions options;
        options.grim = kFakeGrim;
        options.pointer = true;
        const GrabResult result = grabOutputs(pick({"DP-2"}), options);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(logLines(m_log).size(), 1);
        QCOMPARE(logLines(m_log)[0].section(QLatin1Char(' '), 1), QStringLiteral("-c -t ppm -o DP-2 -"));
        // The fake marks the pointer as a white (0, 0).
        QCOMPARE(QColor(result.images.value("DP-2").pixel(0, 0)), QColor(255, 255, 255));
    }

    void wrongSizeIsAnError()
    {
        // The fake gives DP-2 at its logical size, 2560/1.25 x 1600/1.25 = 2048x1280: a grab that lost the scale.
        qputenv("FAKE_GRIM_LOGICAL", "DP-2");
        GrabOptions options;
        options.grim = kFakeGrim;
        const GrabResult result = grabOutputs(pick({"DP-1", "DP-2"}), options);
        QVERIFY(!result.ok());
        QVERIFY2(result.error.contains("DP-2"), qPrintable(result.error));
        QVERIFY2(result.error.contains("2048x1280"), qPrintable(result.error));
        QVERIFY2(result.error.contains("2560x1600"), qPrintable(result.error));
        QCOMPARE(result.images.value("DP-1").size(), QSize(3440, 1440));
    }

    void failingGrimIsAnError()
    {
        qputenv("FAKE_GRIM_FAIL", "DP-1");
        GrabOptions options;
        options.grim = kFakeGrim;
        const GrabResult result = grabOutputs(pick({"DP-1", "DP-2"}), options);
        QVERIFY(!result.ok());
        QVERIFY2(result.error.contains("DP-1"), qPrintable(result.error));
        QVERIFY(!result.images.contains("DP-1"));
        QCOMPARE(result.images.value("DP-2").size(), QSize(2560, 1600));
    }

    void hyprctl()
    {
        qputenv("RILEYSHOT_HYPRCTL", RILEYSHOT_SOURCE_DIR "/tests/fake/hyprctl");
        QFile file(QStringLiteral(RILEYSHOT_TEST_DATA "/monitors.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QString error;
        QCOMPARE(runHyprctl({"monitors", "-j"}, &error), file.readAll());
        QVERIFY(error.isEmpty());
        QVERIFY(runHyprctl({"bogus"}, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        qunsetenv("RILEYSHOT_HYPRCTL");
    }
};

QTEST_MAIN(TestGrab)
#include "tst_grab.moc"
