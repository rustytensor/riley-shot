// SPDX-License-Identifier: GPL-3.0-or-later
// M1 gate: config.xml load and save (builder B: config.cpp) against data/config.xsd. Validation of the written files
// is done by xmllint (libxml2's own command-line validator), not by the code under test.
// Expected values: the defaults are the brief's (save folder ~/Pictures/Screenshots, copy and save on, pointer off)
// and config.h's; the file values are the ones written by hand in tests/data/config-*.xml.
#include "config.h"

#include <QFile>
#include <QProcess>
#include <QtTest>

using namespace rileyshot;

Q_DECLARE_METATYPE(rileyshot::Config)

namespace {

const QString kSchema = QStringLiteral(RILEYSHOT_SOURCE_DIR "/data/config.xsd");
const QString kData = QStringLiteral(RILEYSHOT_TEST_DATA);

// xmllint --noout --schema data/config.xsd FILE: exit 0 when FILE validates.
bool xmllintValidates(const QString &file, QString *output)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(QStringLiteral("xmllint"), {"--noout", "--schema", kSchema, file});
    if (!p.waitForFinished(10000))
        return false;
    *output = QString::fromUtf8(p.readAll());
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

Config full()
{
    // tests/data/config-full.xml, field by field.
    Config c;
    c.saveFolder = QStringLiteral("~/Shots/riley test");
    c.fileNamePattern = QStringLiteral("shot-%H%M%S-%%");
    c.copyOnCapture = false;
    c.saveOnCapture = false; // written "0", a valid xs:boolean
    c.pointer = true;
    c.colours.pen = QStringLiteral("lavender");
    c.colours.highlighter = QStringLiteral("#FFEE00"); // kept as written
    c.colours.shape = QStringLiteral("teal");
    c.colours.text = QStringLiteral("text");
    c.colours.marker = QStringLiteral("#00ff7f");
    return c;
}

} // namespace

class TestConfig : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

private slots:
    void defaults()
    {
        const Config c;
        QCOMPARE(c.saveFolder, QStringLiteral("~/Pictures/Screenshots"));
        QCOMPARE(c.fileNamePattern, QStringLiteral("Screenshot_%Y-%m-%d_%H-%M-%S"));
        QCOMPARE(c.copyOnCapture, true);
        QCOMPARE(c.saveOnCapture, true);
        QCOMPARE(c.pointer, false);
        QCOMPARE(c.colours.pen, QStringLiteral("red"));
        QCOMPARE(c.colours.highlighter, QStringLiteral("yellow"));
        QCOMPARE(c.colours.shape, QStringLiteral("red"));
        QCOMPARE(c.colours.text, QStringLiteral("red"));
        QCOMPARE(c.colours.marker, QStringLiteral("accent"));
    }

    void fixturesAgainstSchema()
    {
        QString out;
        QVERIFY2(xmllintValidates(QStringLiteral(RILEYSHOT_SOURCE_DIR "/data/config.example.xml"), &out),
                 qPrintable(out));
        QVERIFY2(xmllintValidates(kData + "/config-full.xml", &out), qPrintable(out));
        QVERIFY2(xmllintValidates(kData + "/config-partial.xml", &out), qPrintable(out));
        QVERIFY2(!xmllintValidates(kData + "/config-bad.xml", &out), "config-bad.xml must not validate");
    }

    void exampleIsTheDefaults()
    {
        Config c;
        c.pointer = true; // something to overwrite
        QString error;
        QVERIFY(Config::load(QStringLiteral(RILEYSHOT_SOURCE_DIR "/data/config.example.xml"), &c, &error));
        QCOMPARE(c, Config());
    }

    void loadsFull()
    {
        Config c;
        QString error;
        QVERIFY2(Config::load(kData + "/config-full.xml", &c, &error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(c.saveFolder, full().saveFolder);
        QCOMPARE(c.fileNamePattern, full().fileNamePattern);
        QCOMPARE(c.colours, full().colours);
        QCOMPARE(c, full());
    }

    void loadsPartial()
    {
        Config c;
        QVERIFY(Config::load(kData + "/config-partial.xml", &c));
        Config want;
        want.pointer = true;
        QCOMPARE(c, want);
    }

    void missingFileIsDefaults()
    {
        Config c = full();
        QString error;
        QVERIFY(Config::load(m_dir.filePath("does-not-exist.xml"), &c, &error));
        QVERIFY(error.isEmpty());
        QCOMPARE(c, Config());
    }

    void invalidFileIsRejected()
    {
        Config c = full();
        QString error;
        QVERIFY(!Config::load(kData + "/config-bad.xml", &c, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(c, Config());

        // Not well-formed at all.
        const QString broken = m_dir.filePath("broken.xml");
        QFile f(broken);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<riley-shot xmlns=\"urn:riley-shot:config\" version=\"1\"><pointer>true</pointer>");
        f.close();
        c = full();
        error.clear();
        QVERIFY(!Config::load(broken, &c, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(c, Config());
    }

    void roundTrip_data()
    {
        QTest::addColumn<Config>("config");
        QTest::newRow("defaults") << Config();
        QTest::newRow("full") << full();
        // Characters XML must escape: & < > " ' in a folder name. A writer that does not escape them produces a file
        // that is not well-formed, which xmllint rejects.
        Config odd;
        odd.saveFolder = QStringLiteral("~/a & b <c> \"d\" 'e'");
        odd.fileNamePattern = QStringLiteral("x&y<%Y>");
        QTest::newRow("escaping") << odd;
    }

    void roundTrip()
    {
        QFETCH(Config, config);
        // A folder that does not exist yet: save() creates it.
        const QString path = m_dir.filePath(QStringLiteral("%1/riley-shot/config.xml").arg(QTest::currentDataTag()));
        QString error;
        QVERIFY2(config.save(path, &error), qPrintable(error));
        QString out;
        QVERIFY2(xmllintValidates(path, &out), qPrintable(out));
        Config back = config == Config() ? full() : Config();
        QVERIFY2(Config::load(path, &back, &error), qPrintable(error));
        QCOMPARE(back, config);
    }

    void resolvedSaveFolder()
    {
        Config c;
        QCOMPARE(c.resolvedSaveFolder("/home/x"), QStringLiteral("/home/x/Pictures/Screenshots"));
        c.saveFolder = "~";
        QCOMPARE(c.resolvedSaveFolder("/home/x"), QStringLiteral("/home/x"));
        c.saveFolder = "/srv/shots";
        QCOMPARE(c.resolvedSaveFolder("/home/x"), QStringLiteral("/srv/shots"));
        c.saveFolder = "~other/shots"; // only "~" and "~/" mean the home folder
        QCOMPARE(c.resolvedSaveFolder("/home/x"), QStringLiteral("~other/shots"));
    }

    void defaultPath()
    {
        const QByteArray home = qgetenv("HOME"), xdg = qgetenv("XDG_CONFIG_HOME");
        const bool hadXdg = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
        qputenv("HOME", "/home/x");
        qputenv("XDG_CONFIG_HOME", "/tmp/xdg");
        QCOMPARE(Config::defaultPath(), QStringLiteral("/tmp/xdg/riley-shot/config.xml"));
        qputenv("XDG_CONFIG_HOME", "");
        QCOMPARE(Config::defaultPath(), QStringLiteral("/home/x/.config/riley-shot/config.xml"));
        qunsetenv("XDG_CONFIG_HOME");
        QCOMPARE(Config::defaultPath(), QStringLiteral("/home/x/.config/riley-shot/config.xml"));
        qputenv("HOME", home);
        if (hadXdg)
            qputenv("XDG_CONFIG_HOME", xdg);
    }

    void schemaIsCarried()
    {
        // The binary carries the schema: the same bytes as data/config.xsd.
        QFile f(kSchema);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(Config::schema(), f.readAll());
    }
};

QTEST_MAIN(TestConfig)
#include "tst_config.moc"
