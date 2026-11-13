// SPDX-License-Identifier: GPL-3.0-or-later
// rileyshot-comparegrab: the M2 gate's live check that the overlay shows the frozen screen pixel for pixel.
//
//   rileyshot-comparegrab BEFORE.png DURING.png WIDTH HEIGHT [SKIP_X,SKIP_Y,SKIP_W,SKIP_H]
//
// BEFORE is `grim -o DP-2` taken before riley-shot started, DURING is `grim -o DP-2` taken while its overlay was up.
// Both must be WIDTH x HEIGHT (DP-2's device size, 2560x1600). Every pixel outside the optional skip rectangle (the
// capture bar, in device pixels, already grown by the caller) must have the same red, green and blue in both.
// Prints PASS/FAIL lines and exits 0 only when every check passes.
//
// The expected value is "identical": the overlay draws riley-shot's own grim grab of the same static screen (the M1
// test pattern, which holds still for the whole check) at 1:1 device pixels, and the pattern's 0/255 channels
// survive any 8-bit path unchanged.
#include <QGuiApplication>
#include <QImage>
#include <QRect>
#include <QStringList>

#include <cstdio>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc != 5 && argc != 6) {
        std::fprintf(stderr, "usage: rileyshot-comparegrab BEFORE.png DURING.png WIDTH HEIGHT [X,Y,W,H]\n");
        return 2;
    }
    const QImage before(QString::fromLocal8Bit(argv[1]));
    const QImage during(QString::fromLocal8Bit(argv[2]));
    const QSize want(QByteArray(argv[3]).toInt(), QByteArray(argv[4]).toInt());
    QRect skip;
    if (argc == 6) {
        const QStringList p = QString::fromLocal8Bit(argv[5]).split(QLatin1Char(','));
        if (p.size() != 4) {
            std::fprintf(stderr, "rileyshot-comparegrab: skip rectangle must be X,Y,W,H\n");
            return 2;
        }
        skip = QRect(p[0].toInt(), p[1].toInt(), p[2].toInt(), p[3].toInt());
    }

    int failures = 0;
    auto check = [&](bool ok, const QString &what) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
        if (!ok)
            ++failures;
    };
    check(before.size() == want, QStringLiteral("grab before the overlay is %1x%2 (want %3x%4)")
                                     .arg(before.width()).arg(before.height()).arg(want.width()).arg(want.height()));
    check(during.size() == want, QStringLiteral("grab under the overlay is %1x%2 (want %3x%4)")
                                     .arg(during.width()).arg(during.height()).arg(want.width()).arg(want.height()));
    if (failures)
        return 1;

    const QImage first = before.convertedTo(QImage::Format_RGB32);
    const QImage second = during.convertedTo(QImage::Format_RGB32);
    long compared = 0, wrong = 0;
    int shown = 0;
    for (int y = 0; y < want.height(); ++y) {
        const auto *a = reinterpret_cast<const QRgb *>(first.constScanLine(y));
        const auto *b = reinterpret_cast<const QRgb *>(second.constScanLine(y));
        for (int x = 0; x < want.width(); ++x) {
            if (skip.contains(x, y))
                continue;
            ++compared;
            if ((a[x] & 0xffffff) != (b[x] & 0xffffff)) {
                ++wrong;
                if (shown < 5) {
                    std::printf("    | device (%d,%d): before #%06x, under the overlay #%06x\n", x, y,
                                a[x] & 0xffffff, b[x] & 0xffffff);
                    ++shown;
                }
            }
        }
    }
    const long total = long(want.width()) * want.height();
    check(compared * 2 > total, QStringLiteral("compared %1 of %2 device pixels (the skipped bar is %3x%4)")
                                    .arg(compared).arg(total).arg(skip.width()).arg(skip.height()));
    check(wrong == 0,
          QStringLiteral("%1 of %2 compared device pixels differ under the overlay").arg(wrong).arg(compared));
    return failures ? 1 : 0;
}
