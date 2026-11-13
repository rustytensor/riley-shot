// SPDX-License-Identifier: GPL-3.0-or-later
// M3 gate: the `edit` command's parsing (cli.h; builder B: src/core/cli.cpp). The expected values come straight from
// the rules written in cli.h.
#include "cli.h"

#include <QtTest>

using namespace rileyshot;
using Command = CliOptions::Command;

Q_DECLARE_METATYPE(rileyshot::CliOptions::Command)

class TestEditorCli : public QObject
{
    Q_OBJECT

private slots:
    void edit_data()
    {
        QTest::addColumn<QStringList>("args");
        QTest::addColumn<Command>("command");
        QTest::addColumn<QString>("inFile");
        QTest::addColumn<int>("exitAfterMs");
        QTest::addColumn<QString>("report");
        QTest::newRow("file") << QStringList{"edit", "/tmp/a.png"} << Command::Edit << "/tmp/a.png" << 0 << "";
        QTest::newRow("test aids") << QStringList{"edit", "a.png", "--exit-after-ms", "1500", "--report", "r.jsonl"}
                                   << Command::Edit << "a.png" << 1500 << "r.jsonl";
        QTest::newRow("file last") << QStringList{"edit", "--exit-after-ms", "20000", "a.png"} << Command::Edit
                                   << "a.png" << 10000 << ""; // capped at 10000 like capture
        QTest::newRow("help") << QStringList{"edit", "a.png", "--help"} << Command::Help << "" << 0 << "";
        QTest::newRow("no file") << QStringList{"edit"} << Command::Error << "" << 0 << "";
        QTest::newRow("empty file") << QStringList{"edit", ""} << Command::Error << "" << 0 << "";
        QTest::newRow("two files") << QStringList{"edit", "a.png", "b.png"} << Command::Error << "" << 0 << "";
        QTest::newRow("mode") << QStringList{"edit", "a.png", "--region"} << Command::Error << "" << 0 << "";
        QTest::newRow("no editor") << QStringList{"edit", "a.png", "--no-editor"} << Command::Error << "" << 0 << "";
        QTest::newRow("outputs") << QStringList{"edit", "a.png", "--outputs", "DP-2"} << Command::Error << "" << 0 << "";
        QTest::newRow("grab option") << QStringList{"edit", "a.png", "--pointer"} << Command::Error << "" << 0 << "";
        QTest::newRow("zero timer") << QStringList{"edit", "--exit-after-ms", "0", "a.png"} << Command::Error << "" << 0
                                    << "";
        QTest::newRow("timer twice") << QStringList{"edit", "a.png", "--exit-after-ms", "5", "--exit-after-ms", "6"}
                                     << Command::Error << "" << 0 << "";
        QTest::newRow("missing value") << QStringList{"edit", "a.png", "--report"} << Command::Error << "" << 0 << "";
        QTest::newRow("not first") << QStringList{"--region", "edit", "a.png"} << Command::Error << "" << 0 << "";
        QTest::newRow("after capture") << QStringList{"capture", "edit"} << Command::Error << "" << 0 << "";
    }

    void edit()
    {
        QFETCH(QStringList, args);
        QFETCH(Command, command);
        QFETCH(QString, inFile);
        QFETCH(int, exitAfterMs);
        QFETCH(QString, report);
        const CliOptions o = parseCli(args);
        QCOMPARE(int(o.command), int(command));
        if (command == Command::Error) {
            QVERIFY(!o.error.isEmpty());
            return;
        }
        if (command == Command::Edit) {
            QCOMPARE(o.inFile, inFile);
            QCOMPARE(o.exitAfterMs, exitAfterMs);
            QCOMPARE(o.reportFile, report);
        }
    }
};

QTEST_MAIN(TestEditorCli)
#include "tst_editor_cli.moc"
