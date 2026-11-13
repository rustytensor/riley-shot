// SPDX-License-Identifier: GPL-3.0-or-later
// M1 gate: command-line parsing (builder B: cli.cpp). Expected values from the brief's usage line and cli.h's rules.
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
};

QTEST_GUILESS_MAIN(TestCli)
#include "tst_cli.moc"
