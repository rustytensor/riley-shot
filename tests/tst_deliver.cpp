// SPDX-License-Identifier: GPL-3.0-or-later
// M1 gate: copy and save (builder B: deliver.cpp). Copying runs tests/fake/wl-copy, never the real clipboard.
#include "deliver.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtTest>

using namespace rileyshot;

namespace {

const QString kFakeCopy = QStringLiteral(RILEYSHOT_SOURCE_DIR "/tests/fake/wl-copy");

// A 3x2 image with six different opaque pixels, worked out to survive PNG exactly (PNG is lossless).
QImage sample()
{
    QImage image(3, 2, QImage::Format_ARGB32);
    image.setPixel(0, 0, qRgba(255, 0, 0, 255));
    image.setPixel(1, 0, qRgba(0, 255, 0, 255));
    image.setPixel(2, 0, qRgba(0, 0, 255, 255));
    image.setPixel(0, 1, qRgba(1, 2, 3, 255));
    image.setPixel(1, 1, qRgba(250, 251, 252, 255));
    image.setPixel(2, 1, qRgba(128, 64, 32, 255));
    return image;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

const QDateTime kTime(QDate(2026, 11, 12), QTime(21, 5, 9));

} // namespace

class TestDeliver : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

private slots:
    void init()
    {
        qputenv("FAKE_WLCOPY_DIR", m_dir.path().toLocal8Bit());
        qunsetenv("FAKE_WLCOPY_EXIT");
        QFile::remove(m_dir.filePath("args"));
        QFile::remove(m_dir.filePath("stdin"));
    }

    void commands()
    {
        QCOMPARE(defaultCopyCommand(), (QStringList{"wl-copy", "--type", "image/png"}));
        qputenv("RILEYSHOT_COPY_COMMAND", "/opt/fake/wl-copy --type \"image/png\"");
        QCOMPARE(copyCommandFromEnvironment(), (QStringList{"/opt/fake/wl-copy", "--type", "image/png"}));
        qputenv("RILEYSHOT_COPY_COMMAND", "");
        QCOMPARE(copyCommandFromEnvironment(), defaultCopyCommand());
        qunsetenv("RILEYSHOT_COPY_COMMAND");
        QCOMPARE(copyCommandFromEnvironment(), defaultCopyCommand());
    }

    void copiesPng()
    {
        QString error;
        QVERIFY2(copyImage(sample(), {kFakeCopy, "--type", "image/png"}, &error), qPrintable(error));
        QCOMPARE(readAll(m_dir.filePath("args")), QByteArray("--type\nimage/png\n"));
        const QByteArray png = readAll(m_dir.filePath("stdin"));
        // The PNG signature: 89 50 4E 47 0D 0A 1A 0A.
        QVERIFY(png.startsWith(QByteArray::fromHex("89504e470d0a1a0a")));
        QImage back;
        QVERIFY(back.loadFromData(png, "PNG"));
        QCOMPARE(back.convertToFormat(QImage::Format_ARGB32), sample());
    }

    void noShell()
    {
        // Arguments reach the program as they are: a space stays inside one argument and $(...) is not run.
        const QString marker = m_dir.filePath("ran");
        QVERIFY(copyImage(sample(), {kFakeCopy, "a b", QStringLiteral("$(touch %1)").arg(marker)}));
        QCOMPARE(readAll(m_dir.filePath("args")), QStringLiteral("a b\n$(touch %1)\n").arg(marker).toUtf8());
        QVERIFY(!QFileInfo::exists(marker));
    }

    void copyFailures()
    {
        qputenv("FAKE_WLCOPY_EXIT", "1");
        QString error;
        QVERIFY(!copyImage(sample(), {kFakeCopy, "--type", "image/png"}, &error));
        QVERIFY(!error.isEmpty());
        qunsetenv("FAKE_WLCOPY_EXIT");
        error.clear();
        QVERIFY(!copyImage(sample(), {m_dir.filePath("no-such-program")}, &error));
        QVERIFY(!error.isEmpty());
    }

    void fileNames_data()
    {
        QTest::addColumn<QString>("pattern");
        QTest::addColumn<QDateTime>("time");
        QTest::addColumn<QString>("extension");
        QTest::addColumn<QString>("name");
        QTest::newRow("default") << "Screenshot_%Y-%m-%d_%H-%M-%S" << kTime << "png"
                                 << "Screenshot_2026-11-12_21-05-09.png";
        QTest::newRow("padding") << "shot-%H%M%S-%%" << QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5)) << "jpg"
                                 << "shot-030405-%.jpg";
        QTest::newRow("slash") << "a/b_%Y" << kTime << "png" << "a_b_2026.png";
        QTest::newRow("unknown token") << "keep %q %" << kTime << "png" << "keep %q %.png";
        QTest::newRow("escaped") << "%%Y" << kTime << "png" << "%Y.png";
    }

    void fileNames()
    {
        QFETCH(QString, pattern);
        QFETCH(QDateTime, time);
        QFETCH(QString, extension);
        QFETCH(QString, name);
        QCOMPARE(fileNameFor(pattern, time, extension), name);
    }

    void saves()
    {
        const QString folder = m_dir.filePath("new/dir"); // does not exist yet
        QString error;
        const QString first = saveImage(sample(), folder, "shot_%H-%M-%S", kTime, "png", &error);
        QCOMPARE(first, folder + "/shot_21-05-09.png");
        const QString second = saveImage(sample(), folder, "shot_%H-%M-%S", kTime, "png", &error);
        QCOMPARE(second, folder + "/shot_21-05-09-2.png");
        const QString third = saveImage(sample(), folder, "shot_%H-%M-%S", kTime, "png", &error);
        QCOMPARE(third, folder + "/shot_21-05-09-3.png");
        QCOMPARE(QImage(first).convertToFormat(QImage::Format_ARGB32), sample());
        QCOMPARE(QImage(third).convertToFormat(QImage::Format_ARGB32), sample());

        const QString jpeg = saveImage(sample(), folder, "shot_%H-%M-%S", kTime, "jpg", &error);
        QCOMPARE(jpeg, folder + "/shot_21-05-09.jpg");
        QCOMPARE(QImage(jpeg).size(), QSize(3, 2));
        QVERIFY(readAll(jpeg).startsWith(QByteArray::fromHex("ffd8ff"))); // the JPEG signature
    }

    void saveFailure()
    {
        QString error;
        QCOMPARE(saveImage(sample(), "/proc/riley-shot-test", "x", kTime, "png", &error), QString());
        QVERIFY(!error.isEmpty());
    }
};

QTEST_MAIN(TestDeliver)
#include "tst_deliver.moc"
