// SPDX-License-Identifier: GPL-3.0-or-later
// M1 gate: the Riley theme from bar.json (builder B: theme.cpp).
// Expected values: the defaults are riley bar/Theme.qml's; the file values are written by hand in tests/data/bar*.json.
#include "theme.h"

#include <QtTest>

using namespace rileyshot;

namespace {
const QString kData = QStringLiteral(RILEYSHOT_TEST_DATA);
}

class TestTheme : public QObject
{
    Q_OBJECT

private slots:
    void defaults()
    {
        const Theme t;
        QCOMPARE(t.base, QColor("#151a23"));
        QCOMPARE(t.accent, QColor("#e9a046"));
        QCOMPARE(t.red, QColor("#e5776a"));
        QCOMPARE(t.yellow, QColor("#e3c169"));
        QCOMPARE(t.font, QStringLiteral("IBM Plex Mono"));
        QCOMPARE(Theme::keys(), (QStringList{"base", "surface0", "surface1", "text", "subtext", "accent", "mauve",
                                             "lavender", "teal", "peach", "green", "red", "yellow"}));
    }

    void loadsEveryKey()
    {
        // tests/data/bar.json sets every key to a value unlike its default.
        const Theme t = Theme::load(kData + "/bar.json");
        QCOMPARE(t.base, QColor("#010203"));
        QCOMPARE(t.surface0, QColor("#111213"));
        QCOMPARE(t.surface1, QColor("#212223"));
        QCOMPARE(t.text, QColor("#fefdfc"));
        QCOMPARE(t.subtext, QColor("#c0c1c2"));
        QCOMPARE(t.accent, QColor("#a1b2c3"));
        QCOMPARE(t.mauve, QColor("#808182"));
        QCOMPARE(t.lavender, QColor("#707172"));
        QCOMPARE(t.teal, QColor("#606162"));
        QCOMPARE(t.peach, QColor("#505152"));
        QCOMPARE(t.green, QColor("#404142"));
        QCOMPARE(t.red, QColor("#ff0001"));
        QCOMPARE(t.yellow, QColor("#fefe00"));
        QCOMPARE(t.font, QStringLiteral("Gate Test Mono"));
    }

    void partialKeepsDefaults()
    {
        // bar-partial.json: red valid; green not a colour; yellow a number; font empty. Only red changes.
        const Theme t = Theme::load(kData + "/bar-partial.json");
        Theme want;
        want.red = QColor("#123456");
        QCOMPARE(t, want);
    }

    void brokenOrMissingIsDefaults()
    {
        QCOMPARE(Theme::load(kData + "/bar-broken.json"), Theme());
        QCOMPARE(Theme::load(kData + "/no-such-file.json"), Theme());
    }

    void lookup()
    {
        const Theme t = Theme::load(kData + "/bar.json");
        QCOMPARE(t.colour("accent"), QColor("#a1b2c3"));
        QVERIFY(!t.colour("orange").isValid());
        QCOMPARE(t.resolve("teal"), QColor("#606162"));
        QCOMPARE(t.resolve("#00ff7f"), QColor(0, 255, 127));
        QCOMPARE(t.resolve("#FFEE00"), QColor(255, 238, 0));
        QVERIFY(!t.resolve("orange").isValid());
        QVERIFY(!t.resolve("#12345").isValid());
    }

    void palette()
    {
        // red, peach, yellow, green, teal, lavender, mauve, text from bar.json.
        const Theme t = Theme::load(kData + "/bar.json");
        QCOMPARE(t.palette(), (QList<QColor>{QColor("#ff0001"), QColor("#505152"), QColor("#fefe00"),
                                             QColor("#404142"), QColor("#606162"), QColor("#707172"),
                                             QColor("#808182"), QColor("#fefdfc")}));
    }

    void defaultPath()
    {
        const QByteArray home = qgetenv("HOME");
        qputenv("HOME", "/home/x");
        QCOMPARE(Theme::defaultPath(), QStringLiteral("/home/x/.config/riley/bar.json"));
        qputenv("HOME", home);
    }
};

QTEST_MAIN(TestTheme)
#include "tst_theme.moc"
