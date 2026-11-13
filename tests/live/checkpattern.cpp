// SPDX-License-Identifier: GPL-3.0-or-later
// rileyshot-checkpattern FILE.png: checks a grab of DP-2 taken while rileyshot-testpattern was showing.
// Prints one PASS/FAIL line per check and exits 0 only when all pass.
//
// Expected values come from pattern.h (the quadrant colours and the 1-device-pixel features), which are fixed by hand
// from DP-2's device size 2560x1600; see the derivations there. The grab must be the output's device pixels, so the
// image itself must be 2560x1600: a grab at logical size would be 2048x1280 (2560/1.25, 1600/1.25).
// Colours must match exactly: the pattern uses only 0 and 255 channel values, which no 8-bit path changes.
#include "pattern.h"

#include <QGuiApplication>
#include <QImage>

#include <cstdio>

namespace {

int failures = 0;

void expect(const QImage &image, int x, int y, const QColor &want, const char *what)
{
    const QColor got = image.pixelColor(x, y);
    const bool ok = got.red() == want.red() && got.green() == want.green() && got.blue() == want.blue();
    std::printf("%s pattern %s at device (%d,%d): want %s got %s\n", ok ? "PASS" : "FAIL", what, x, y,
                qPrintable(want.name()), qPrintable(got.name()));
    if (!ok)
        ++failures;
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc != 2) {
        std::fprintf(stderr, "usage: rileyshot-checkpattern FILE.png\n");
        return 2;
    }
    const QImage image(QString::fromLocal8Bit(argv[1]));
    const bool sized = image.size() == QSize(pattern::kWidth, pattern::kHeight);
    std::printf("%s pattern image size: want 2560x1600 got %dx%d\n", sized ? "PASS" : "FAIL", image.width(),
                image.height());
    if (!sized)
        return 1;

    using namespace pattern;
    // Quadrant corners and centres: the grab covers the whole output with no offset and no flip.
    expect(image, 0, 0, kRed, "top-left corner");
    expect(image, 640, 400, kRed, "top-left centre");
    expect(image, 1279, 799, kRed, "top-left quadrant's last pixel");
    expect(image, 1280, 0, kGreen, "top-right quadrant's first pixel");
    expect(image, 2559, 0, kGreen, "top-right corner");
    expect(image, 1920, 400, kGreen, "top-right centre");
    expect(image, 0, 800, kBlue, "bottom-left quadrant's first pixel");
    expect(image, 0, 1599, kBlue, "bottom-left corner");
    expect(image, 1279, 1599, kBlue, "bottom-left quadrant's last pixel");
    expect(image, 2559, 1599, kBlack, "bottom-right corner");

    // Alignment: each 1-device-pixel feature on its exact device pixel, its neighbours untouched.
    expect(image, kDot.x(), kDot.y(), kMagenta, "dot (logical 1280.8,800.8)");
    expect(image, kDot.x() - 1, kDot.y(), kBlack, "left of dot");
    expect(image, kDot.x() + 1, kDot.y(), kBlack, "right of dot");
    expect(image, kDot.x(), kDot.y() - 1, kBlack, "above dot");
    expect(image, kDot.x(), kDot.y() + 1, kBlack, "below dot");
    expect(image, kVLineX, 1200, kYellow, "vertical line");
    expect(image, kVLineX - 1, 1200, kBlack, "left of vertical line");
    expect(image, kVLineX + 1, 1200, kBlack, "right of vertical line");
    expect(image, kVLineX, kVLineTop, kYellow, "vertical line top end");
    expect(image, kVLineX, kVLineTop - 1, kBlack, "above vertical line");
    expect(image, kVLineX, kVLineBottom, kYellow, "vertical line bottom end");
    expect(image, kVLineX, kVLineBottom + 1, kBlack, "below vertical line");
    expect(image, 1500, kHLineY, kCyan, "horizontal line");
    expect(image, 1500, kHLineY - 1, kBlack, "above horizontal line");
    expect(image, 1500, kHLineY + 1, kBlack, "below horizontal line");
    expect(image, kHLineLeft - 1, kHLineY, kBlack, "left of horizontal line");
    expect(image, kHLineRight + 1, kHLineY, kBlack, "right of horizontal line");

    return failures == 0 ? 0 : 1;
}
