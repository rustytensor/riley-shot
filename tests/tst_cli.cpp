// SPDX-License-Identifier: GPL-3.0-or-later
// M1 and M2 gates: command-line parsing (cli.cpp: M1 builder B, M2 builder A). Expected values from the brief's
// usage line and cli.h's rules.
#include "cli.h"

#include <QtTest>

using namespace rileyshot;
using Command = CliOptions::Command;

Q_DECLARE_METATYPE(rileyshot::Mode)
Q_DECLARE_METATYPE(rileyshot::CliOptions::Command)

class TestCli : public QObject
{
    Q_OBJECT

private slots:
    void parse_data()
    {
        QTest::addColumn<QStringList>("args");
        QTest::addColumn<Command>("command");
        QTest::addColumn<Mode>("mode");
        QTest::addColumn<bool>("editor");
        QTest::newRow("nothing") << QStringList{} << Command::Capture << Mode::Bar << true;
        QTest::newRow("capture") << QStringList{"capture"} << Command::Capture << Mode::Bar << true;
        QTest::newRow("region") << QStringList{"--region"} << Command::Capture << Mode::Region << true;
        QTest::newRow("capture screen") << QStringList{"capture", "--screen"} << Command::Capture << Mode::Screen
                                        << true;
        QTest::newRow("window no editor") << QStringList{"--window", "--no-editor"} << Command::Capture
                                          << Mode::Window << false;
        QTest::newRow("no editor first") << QStringList{"capture", "--no-editor", "--region"} << Command::Capture
                                         << Mode::Region << false;
        QTest::newRow("help") << QStringList{"--help"} << Command::Help << Mode::Bar << true;
        QTest::newRow("capture -h") << QStringList{"capture", "-h"} << Command::Help << Mode::Bar << true;
        QTest::newRow("two modes") << QStringList{"--region", "--screen"} << Command::Error << Mode::Bar << true;
        QTest::newRow("twice") << QStringList{"--no-editor", "--no-editor"} << Command::Error << Mode::Bar << true;
        QTest::newRow("word after option") << QStringList{"--screen", "capture"} << Command::Error << Mode::Bar
                                           << true;
        QTest::newRow("unknown option") << QStringList{"--bogus"} << Command::Error << Mode::Bar << true;
        QTest::newRow("unknown word") << QStringList{"shoot"} << Command::Error << Mode::Bar << true;
        QTest::newRow("grab option on capture") << QStringList{"capture", "--output", "DP-2"} << Command::Error
                                                << Mode::Bar << true;
        QTest::newRow("capture option on grab") << QStringList{"grab", "--output", "DP-2", "--out", "/x.png",
                                                               "--region"}
                                                << Command::Error << Mode::Bar << true;
        QTest::newRow("grab without out") << QStringList{"grab", "--output", "DP-2"} << Command::Error << Mode::Bar
                                          << true;
        QTest::newRow("grab missing value") << QStringList{"grab", "--out", "/x.png", "--output"} << Command::Error
                                            << Mode::Bar << true;
    }

    void parse()
    {
        QFETCH(QStringList, args);
        QFETCH(Command, command);
        QFETCH(Mode, mode);
        QFETCH(bool, editor);
        const CliOptions o = parseCli(args);
        QCOMPARE(o.command, command);
        QCOMPARE(o.error.isEmpty(), command != Command::Error);
        if (command == Command::Capture) {
            QCOMPARE(o.mode, mode);
            QCOMPARE(o.editor, editor);
        }
    }

    void grab()
    {
        CliOptions o = parseCli({"grab", "--output", "DP-2", "--out", "/tmp/x.png"});
        QCOMPARE(o.command, Command::Grab);
        QCOMPARE(o.output, QStringLiteral("DP-2"));
        QCOMPARE(o.outFile, QStringLiteral("/tmp/x.png"));
        QCOMPARE(o.pointer, false);
        o = parseCli({"grab", "--pointer", "--out", "/tmp/y.png", "--output", "HDMI-A-1"});
        QCOMPARE(o.command, Command::Grab);
        QCOMPARE(o.output, QStringLiteral("HDMI-A-1"));
        QCOMPARE(o.outFile, QStringLiteral("/tmp/y.png"));
        QCOMPARE(o.pointer, true);
    }

    // M2: the capture test aids (cli.h). Expected values from cli.h's rules for --outputs, --exit-after-ms and
    // --report.
    void testAids()
    {
        CliOptions o = parseCli({"--outputs", "DP-2", "--exit-after-ms", "8000", "--report", "/run/r.jsonl"});
        QCOMPARE(o.command, Command::Capture);
        QCOMPARE(o.mode, Mode::Bar);
        QCOMPARE(o.outputs, QStringList{"DP-2"});
        QCOMPARE(o.exitAfterMs, 8000);
        QCOMPARE(o.reportFile, QStringLiteral("/run/r.jsonl"));

        o = parseCli({"capture", "--region", "--outputs", "DP-2,HDMI-A-2", "--no-editor"});
        QCOMPARE(o.command, Command::Capture);
        QCOMPARE(o.mode, Mode::Region);
        QCOMPARE(o.editor, false);
        QCOMPARE(o.outputs, (QStringList{"DP-2", "HDMI-A-2"}));
        QCOMPARE(o.exitAfterMs, 0); // no timer unless asked
        QCOMPARE(o.reportFile, QString());

        // Above 10000 ms it becomes 10000: a test overlay never stays up longer than 10 s.
        o = parseCli({"--exit-after-ms", "60000"});
        QCOMPARE(o.command, Command::Capture);
        QCOMPARE(o.exitAfterMs, 10000);
        o = parseCli({"--exit-after-ms", "10000"});
        QCOMPARE(o.exitAfterMs, 10000);
        o = parseCli({"--exit-after-ms", "1"});
        QCOMPARE(o.exitAfterMs, 1);

        // Errors.
        for (const QStringList &bad : QList<QStringList>{
                 {"--exit-after-ms", "0"},
                 {"--exit-after-ms", "-5"},
                 {"--exit-after-ms", "abc"},
                 {"--exit-after-ms"},
                 {"--outputs"},
                 {"--outputs", ""},
                 {"--outputs", "DP-2,,DP-1"},
                 {"--outputs", "DP-2,"},
                 {"--report"},
                 {"--outputs", "DP-2", "--outputs", "DP-1"},
                 {"--exit-after-ms", "100", "--exit-after-ms", "200"},
                 {"--report", "/a", "--report", "/b"},
                 {"grab", "--output", "DP-2", "--out", "/x.png", "--outputs", "DP-2"},
                 {"grab", "--output", "DP-2", "--out", "/x.png", "--exit-after-ms", "100"},
             }) {
            o = parseCli(bad);
            QVERIFY2(o.command == Command::Error, qPrintable(bad.join(' ')));
            QVERIFY2(!o.error.isEmpty(), qPrintable(bad.join(' ')));
        }
    }
};

QTEST_GUILESS_MAIN(TestCli)
#include "tst_cli.moc"
