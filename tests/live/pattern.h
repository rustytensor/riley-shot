// SPDX-License-Identifier: GPL-3.0-or-later
// The test pattern the M1 gate shows on DP-2 and then finds again in the grab. One definition, shared by the window
// that draws it (pattern.cpp) and the checker that reads the grab (checkpattern.cpp), so the two cannot drift apart.
//
// Everything here is in DP-2's DEVICE pixels. DP-2 is 2560x1600 at scale 1.25 (logical 2048x1280), so a device pixel
// is 0.8 logical pixels and most device coordinates fall between logical pixels.
#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QSize>

namespace pattern {

// hyprctl monitors -j on this desktop: DP-2 width 2560, height 1600, transform 0 -> device size 2560x1600.
inline constexpr int kWidth = 2560;
inline constexpr int kHeight = 1600;

// Quadrants: the device size halved, 2560/2 = 1280 and 1600/2 = 800.
//   top-left     (0,0)      .. (1279,799)   red    #ff0000
//   top-right    (1280,0)   .. (2559,799)   green  #00ff00
//   bottom-left  (0,800)    .. (1279,1599)  blue   #0000ff
//   bottom-right (1280,800) .. (2559,1599)  black  #000000, carrying the 1-device-pixel features below.
// The quadrant edges, x=1280 and y=800, sit on logical pixel edges (1280/1.25 = 1024, 800/1.25 = 640).
inline constexpr int kHalfX = 1280;
inline constexpr int kHalfY = 800;

// The 1-device-pixel features in the black quadrant. None of them sits on a logical pixel edge:
//   the dot at device (1601,1001)      = logical (1280.8, 800.8)
//   the vertical line at device x=2001 = logical x 1600.8, device y 900..1499 inclusive
//   the horizontal line at device y=1201 = logical y 960.8, device x 1400..1599 inclusive
// Anything that resamples through logical pixels (one window across the layout, a logical-sized grab scaled up)
// smears or shifts them by a device pixel or more, and the neighbours stop being black.
inline constexpr QPoint kDot{1601, 1001};
inline constexpr int kVLineX = 2001, kVLineTop = 900, kVLineBottom = 1499;
inline constexpr int kHLineY = 1201, kHLineLeft = 1400, kHLineRight = 1599;

inline const QColor kRed{255, 0, 0}, kGreen{0, 255, 0}, kBlue{0, 0, 255}, kBlack{0, 0, 0};
inline const QColor kMagenta{255, 0, 255}, kYellow{255, 255, 0}, kCyan{0, 255, 255};

// Paints the pattern into an image of exactly kWidth x kHeight device pixels, pixel by pixel (no painter, no
// scaling, no antialiasing).
inline QImage render()
{
    QImage image(kWidth, kHeight, QImage::Format_RGB32);
    for (int y = 0; y < kHeight; ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < kWidth; ++x) {
            QColor c = x < kHalfX ? (y < kHalfY ? kRed : kBlue) : (y < kHalfY ? kGreen : kBlack);
            line[x] = c.rgb();
        }
    }
    image.setPixelColor(kDot, kMagenta);
    for (int y = kVLineTop; y <= kVLineBottom; ++y)
        image.setPixelColor(kVLineX, y, kYellow);
    for (int x = kHLineLeft; x <= kHLineRight; ++x)
        image.setPixelColor(x, kHLineY, kCyan);
    return image;
}

} // namespace pattern
