// SPDX-License-Identifier: GPL-3.0-or-later
// M3 gate: the editor's document (builder A: src/editor/document.cpp), offscreen. Every tool on a known canvas,
// checked at hand-worked pixels; undo and redo to the exact pixel; move, resize, delete; crop; save (PNG, JPEG) and
// copy through a fake wl-copy.
//
// Conventions for the hand-worked values (document.h, "How render() draws"):
//   - Image pixel (x,y) covers the square [x,x+1) x [y,y+1). A stroke of width w centred on a path covers the band
//     within w/2 of it. A pixel whose whole square lies inside a drawn shape is that shape's colour exactly (up to
//     antialiasing rounding: tolerance T = 3 per channel); a pixel whose whole square lies more than 1 px away from
//     every shape is the background exactly. Only such pixels are sampled; edge pixels never are.
//   - Theme colours (theme.h defaults): red #e5776a = (229,119,106), yellow #e3c169 = (227,193,105),
//     accent #e9a046 = (233,160,70), green #9fc184, mauve #bf98d9.
//   - Canvases: white(200,150) all #ffffff; checker(160,120) black/white 8x8 blocks, white where
//     (x div 8 + y div 8) is odd; gradient(200,150) pixel (x,y) = rgb(x, y, 0).
#include "annotation.h"
#include "document.h"

#include <QBuffer>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTime>
#include <QUrl>
#include <QtTest>

#include <cmath>

using namespace rileyshot;

namespace {

const int T = 3;
const QRgb kWhite = qRgb(255, 255, 255);
const QRgb kBlack = qRgb(0, 0, 0);
const QColor kRed(0xe5, 0x77, 0x6a);
const QColor kYellow(0xe3, 0xc1, 0x69);
const QColor kAccent(0xe9, 0xa0, 0x46);
const QColor kGreen(0x9f, 0xc1, 0x84);
const QColor kMauve(0xbf, 0x98, 0xd9);

QImage white(int w, int h)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(kWhite);
    return image;
}

QImage checker(int w, int h)
{
    QImage image(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            image.setPixel(x, y, ((x / 8 + y / 8) % 2) ? kWhite : kBlack);
    return image;
}

QImage gradient(int w, int h)
{
    QImage image(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            image.setPixel(x, y, qRgb(x, y, 0));
    return image;
}

bool near(QRgb a, QRgb b, int tol)
{
    return qAbs(qRed(a) - qRed(b)) <= tol && qAbs(qGreen(a) - qGreen(b)) <= tol && qAbs(qBlue(a) - qBlue(b)) <= tol;
}

QString hex(QRgb c)
{
    return QStringLiteral("#%1").arg(c & 0xffffff, 6, 16, QLatin1Char('0'));
}

int differing(const QImage &a, const QImage &b)
{
    if (a.size() != b.size())
        return -1;
    int n = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if ((a.pixel(x, y) & 0xffffff) != (b.pixel(x, y) & 0xffffff))
                ++n;
    return n;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

#define PIX(img, x, y, want, tol)                                                                                   \
    do {                                                                                                            \
        const QRgb got_ = (img).pixel((x), (y));                                                                    \
        QVERIFY2(near(got_, (want), (tol)), qPrintable(QStringLiteral("pixel (%1,%2) is %3, want %4 (±%5)")         \
                                                           .arg(x).arg(y).arg(hex(got_)).arg(hex(want)).arg(tol))); \
    } while (0)

// Two images equal pixel for pixel (format and size included).
#define SAME(a, b)                                                                                                  \
    do {                                                                                                            \
        const QImage a_ = (a), b_ = (b);                                                                            \
        QCOMPARE(a_.size(), b_.size());                                                                             \
        QCOMPARE(a_.format(), b_.format());                                                                         \
        QVERIFY2(a_ == b_, qPrintable(QStringLiteral("%1 pixels differ").arg(differing(a_, b_))));                  \
    } while (0)

class TestDocument : public QObject
{
    Q_OBJECT

private slots:
    // --- the document's starting state -------------------------------------------------------------------------
    void defaults()
    {
        Document d;
        d.setImage(white(200, 150));
        QCOMPARE(d.tool(), int(Document::Pen));
        QCOMPARE(d.strokeWidth(), 4.0);
        QCOMPARE(d.filled(), false);
        QCOMPARE(d.textSize(), 24.0);
        QCOMPARE(d.colour().rgb(), kRed.rgb());
        QCOMPARE(d.toolColour(Document::Highlighter).rgb(), kYellow.rgb());
        QCOMPARE(d.toolColour(Document::Marker).rgb(), kAccent.rgb());
        QCOMPARE(d.toolColour(Document::Arrow).rgb(), kRed.rgb());
        QCOMPARE(d.markerTextColour().rgb(), kWhite);
        QCOMPARE(d.imageSize(), QSize(200, 150));
        QCOMPARE(d.cropRect(), QRect(0, 0, 200, 150));
        QCOMPARE(d.outputSize(), QSize(200, 150));
        QCOMPARE(d.annotationCount(), 0);
        QCOMPARE(d.selectedId(), -1);
        QVERIFY(!d.canUndo());
        QVERIFY(!d.canRedo());
        // Nothing drawn yet: the render is the shot itself, as Format_RGB32.
        SAME(d.render(), white(200, 150));
        // A stroke width below 1 becomes 1.
        d.setStrokeWidth(0.2);
        QCOMPARE(d.strokeWidth(), 1.0);
    }

    void toolColours()
    {
        Document d;
        d.setImage(white(200, 150));
        d.setTool(Document::Highlighter);
        QCOMPARE(d.colour().rgb(), kYellow.rgb());
        d.setColour(kGreen);
        QCOMPARE(d.toolColour(Document::Highlighter).rgb(), kGreen.rgb());
        QCOMPARE(d.toolColour(Document::Pen).rgb(), kRed.rgb());
        d.setTool(Document::Pen);
        QCOMPARE(d.colour().rgb(), kRed.rgb());
        d.setTool(Document::Highlighter);
        QCOMPARE(d.colour().rgb(), kGreen.rgb()); // each tool keeps its own colour
        d.setTool(99);
        QCOMPARE(d.tool(), int(Document::Highlighter)); // out of range: ignored
    }

    // --- each tool's pixels ------------------------------------------------------------------------------------
    void pen()
    {
        // An L: (20,40) -> (100,40) -> (100,120), width 6, round caps and joins.
        // Horizontal leg covers y in [37,43] for x in [20,100] (+3 round cap at x=17); vertical leg x in [97,103].
        Document d;
        d.setImage(white(200, 150));
        const int id = d.addStroke(Annotation::Pen, {QPointF(20, 40), QPointF(100, 40), QPointF(100, 120)}, kRed, 6);
        QVERIFY(id > 0);
        QCOMPARE(d.annotation(id).kind, Annotation::Pen);
        QCOMPARE(d.annotation(id).points.size(), 3);
        const QImage r = d.render();
        PIX(r, 60, 40, kRed.rgb(), T);  // square [60,61)x[40,41) inside the band [37,43]
        PIX(r, 100, 80, kRed.rgb(), T); // x [100,101) inside [97,103]
        PIX(r, 60, 48, kWhite, T);      // y 48 is 5 below the band's edge 43
        PIX(r, 60, 80, kWhite, T);      // inside the L, 37 px from either leg
        PIX(r, 100, 125, kWhite, T);    // the cap ends at y 123
        PIX(r, 14, 40, kWhite, T);      // the cap ends at x 17
        QCOMPARE(d.annotationBounds(id), QRectF(20, 40, 80, 80)); // points' bounding rect
    }

    void highlighter()
    {
        // The path crosses itself: (20,50) -> (80,50) -> (50,20) -> (50,80), width 10, yellow at alpha 128 over white.
        // SourceOver, premultiplied: channel = c * 128/255 + 255 * 127/255:
        //   R 227*128/255 = 113.9 -> 114, + 127 = 241;  G 193*128/255 = 96.9 -> 97, + 127 = 224;
        //   B 105*128/255 = 52.7 -> 53, + 127 = 180.   Expected (241,224,180), tolerance 2.
        // Applied twice it would be R 227*.502 + 241*.498 = 234, G 208, B 142: clearly different.
        const QRgb once = qRgb(241, 224, 180);
        const QList<QPointF> path{QPointF(20, 50), QPointF(80, 50), QPointF(50, 20), QPointF(50, 80)};
        Document d;
        d.setImage(white(200, 150));
        d.addStroke(Annotation::Highlighter, path, kYellow, 10);
        const QImage r = d.render();
        // (35,50): inside the first leg's band y [45,55]; 31.8 px from the diagonal leg (x - y = 30), 15 from x = 50.
        PIX(r, 35, 50, once, 2);
        // (50,50): where the first leg (y [45,55]) and the last leg (x [45,55]) overlap; 21.2 px from the diagonal.
        // Still applied once.
        PIX(r, 50, 50, once, 2);
        PIX(r, 35, 62, kWhite, T); // 7 px below the first leg's band
        // The colour's own alpha does not matter: always 128.
        Document e;
        e.setImage(white(200, 150));
        QColor faint = kYellow;
        faint.setAlpha(40);
        e.addStroke(Annotation::Highlighter, path, faint, 10);
        PIX(e.render(), 35, 50, once, 2);
    }

    void line()
    {
        // Horizontal (20,75) -> (180,75), width 6: band y [72,78].
        Document d;
        d.setImage(white(200, 150));
        const int id = d.addLine(QPointF(20, 75), QPointF(180, 75), kRed, 6);
        QVERIFY(id > 0);
        QImage r = d.render();
        PIX(r, 100, 75, kRed.rgb(), T);
        PIX(r, 100, 73, kRed.rgb(), T); // [73,74) inside [72,78]
        PIX(r, 100, 69, kWhite, T);     // [69,70): 2 above the band
        PIX(r, 100, 80, kWhite, T);     // [80,81): 2 below
        // Diagonal (20,20) -> (180,130), width 6. Direction (160,110), length sqrt(37700) = 194.16.
        // Pixel (100,75), centre (100.5,75.5): offset (80.5,55.5), cross 80.5*110 - 55.5*160 = -25, distance 0.13;
        // its farthest corner is 0.13 + 0.71 < 3 from the line: wholly inside.
        // Pixel (100,60), centre (100.5,60.5): cross 80.5*110 - 40.5*160 = 2375, distance 12.2: background.
        Document e;
        e.setImage(white(200, 150));
        e.addLine(QPointF(20, 20), QPointF(180, 130), kRed, 6);
        r = e.render();
        PIX(r, 100, 75, kRed.rgb(), T);
        PIX(r, 100, 60, kWhite, T);
        // A line of no length adds nothing.
        QCOMPARE(e.addLine(QPointF(5, 5), QPointF(5, 5), kRed, 6), -1);
        QCOMPARE(e.annotationCount(), 1);
    }

    void arrow()
    {
        // (20,75) -> (180,75), width 4: head length L = max(16,12) = 16, half-width H = max(8,6) = 8.
        // Tip (180,75), base at x = 164; the head's half-height at x is 8 * (180 - x) / 16 = (180 - x) / 2.
        // Shaft y [73,77], from x 20 (flat) to 172 (inside the head).
        Document d;
        d.setImage(white(200, 150));
        const int id = d.addArrow(QPointF(20, 75), QPointF(180, 75), kRed, 4);
        QVERIFY(id > 0);
        QCOMPARE(d.annotation(id).kind, Annotation::Arrow);
        QImage r = d.render();
        PIX(r, 170, 75, kRed.rgb(), T);  // [170,171): half-height >= 4.5, rows [75,76) inside
        PIX(r, 166, 69, kRed.rgb(), T);  // half-height at x 167 is 6.5: rows from 68.5; [69,70) inside. Not shaft.
        PIX(r, 166, 64, kWhite, T);      // half-height at 166 is 7: head from row 68; [64,65) is 3 outside
        PIX(r, 150, 69, kWhite, T);      // before the head (x < 164) only the shaft [73,77]
        PIX(r, 100, 75, kRed.rgb(), T);  // the shaft
        PIX(r, 183, 75, kWhite, T);      // beyond the tip
        PIX(r, 16, 75, kWhite, T);       // before the flat start at x 20
        PIX(r, 24, 69, kWhite, T);       // no head at the start
        // Pointing up: (100,140) -> (100,20), width 4. Tip (100,20), base at y 36, half-width (y - 20) / 2.
        Document e;
        e.setImage(white(200, 150));
        e.addArrow(QPointF(100, 140), QPointF(100, 20), kRed, 4);
        r = e.render();
        PIX(r, 96, 32, kRed.rgb(), T);  // half-width at y 32 is 6: x [94,106] holds [96,97)
        PIX(r, 100, 24, kRed.rgb(), T); // half-width at y 24 is 2: x [98,102] holds [100,101)
        PIX(r, 100, 16, kWhite, T);     // beyond the tip
        PIX(r, 100, 120, kRed.rgb(), T);// the shaft x [98,102]
        PIX(r, 94, 120, kWhite, T);     // beside the shaft
        PIX(r, 100, 143, kWhite, T);    // below the flat start at y 140
    }

    void rectangle()
    {
        // (40,30) 100x60, width 4, square corners: edges x 40 -> [38,42], x 140 -> [138,142], y 30 -> [28,32],
        // y 90 -> [88,92].
        Document d;
        d.setImage(white(200, 150));
        const int id = d.addRectangle(QRectF(40, 30, 100, 60), kRed, 4, false);
        QVERIFY(id > 0);
        QImage r = d.render();
        PIX(r, 40, 60, kRed.rgb(), T);
        PIX(r, 39, 60, kRed.rgb(), T);
        PIX(r, 139, 60, kRed.rgb(), T);
        PIX(r, 90, 30, kRed.rgb(), T);
        PIX(r, 90, 89, kRed.rgb(), T);
        PIX(r, 38, 28, kRed.rgb(), T); // the outer corner pixel: square (miter) corner
        PIX(r, 90, 60, kWhite, T);     // inside: outline only
        PIX(r, 44, 60, kWhite, T);     // 2 inside the left edge's band
        PIX(r, 35, 60, kWhite, T);     // outside
        PIX(r, 90, 95, kWhite, T);
        QCOMPARE(d.annotationBounds(id), QRectF(40, 30, 100, 60));
        // Filled: the inside too.
        Document e;
        e.setImage(white(200, 150));
        e.addRectangle(QRectF(40, 30, 100, 60), kRed, 4, true);
        r = e.render();
        PIX(r, 90, 60, kRed.rgb(), T);
        PIX(r, 44, 60, kRed.rgb(), T);
        PIX(r, 35, 60, kWhite, T);
        // Given as a negative-size rect, it is normalised; no area adds nothing.
        Document f;
        f.setImage(white(200, 150));
        f.addRectangle(QRectF(140, 90, -100, -60), kRed, 4, false);
        SAME(f.render(), d.render());
        QCOMPARE(f.addRectangle(QRectF(10, 10, 0, 50), kRed, 4, false), -1);
    }

    void ellipse()
    {
        // Inscribed in (40,30) 120x80: centre (100,70), semi-axes 60 and 40, width 4.
        Document d;
        d.setImage(white(200, 150));
        d.addEllipse(QRectF(40, 30, 120, 80), kRed, 4, false);
        QImage r = d.render();
        PIX(r, 159, 70, kRed.rgb(), T); // right end at x 160: band x [158,162] around row 70
        PIX(r, 100, 30, kRed.rgb(), T); // top at y 30: band [28,32]
        PIX(r, 100, 70, kWhite, T);     // centre: outline only
        PIX(r, 44, 70, kWhite, T);      // 2.5 inside the left band [38,42]
        // (42,32), centre (42.5,32.5): (57.5/60)^2 + (37.5/40)^2 = 0.918 + 0.879 = 1.80, far outside the ellipse,
        // though inside its box: an ellipse, not a rectangle.
        PIX(r, 42, 32, kWhite, T);
        Document e;
        e.setImage(white(200, 150));
        e.addEllipse(QRectF(40, 30, 120, 80), kRed, 4, true);
        r = e.render();
        PIX(r, 100, 70, kRed.rgb(), T);
        PIX(r, 44, 70, kRed.rgb(), T);
        PIX(r, 42, 32, kWhite, T);
    }

    void textBox()
    {
        // "Hello" at top-left (30,40), 20 px. The box is (30,40) wide as the advance, tall as the font's height
        // (QFontMetricsF of the same font); all ink lies in it (2 px slack for antialiasing), with ink near both its
        // left and right ends.
        Document d;
        d.setImage(white(200, 150));
        d.setFontFamily(QStringLiteral("Noto Sans"));
        const int id = d.addText(QPointF(30, 40), QStringLiteral("Hello"), kRed, 20);
        QVERIFY(id > 0);
        QCOMPARE(d.annotation(id).kind, Annotation::Text);
        QCOMPARE(d.annotation(id).text, QStringLiteral("Hello"));
        QFont font(QStringLiteral("Noto Sans"));
        font.setPixelSize(20);
        const QFontMetricsF fm(font);
        const QRectF box = d.annotationBounds(id);
        QCOMPARE(box.topLeft(), QPointF(30, 40));
        QVERIFY2(qAbs(box.width() - fm.horizontalAdvance(QStringLiteral("Hello"))) < 0.01,
                 qPrintable(QStringLiteral("box width %1").arg(box.width())));
        QVERIFY2(qAbs(box.height() - fm.height()) < 0.01, qPrintable(QStringLiteral("box height %1").arg(box.height())));
        QVERIFY(box.width() > 20);
        const QImage r = d.render();
        const QRectF slack = box.adjusted(-2, -2, 2, 2);
        int ink = 0, left = 0, right = 0, reddish = 0;
        for (int y = 0; y < r.height(); ++y)
            for (int x = 0; x < r.width(); ++x) {
                const QRgb p = r.pixel(x, y);
                if ((p & 0xffffff) == (kWhite & 0xffffff))
                    continue;
                ++ink;
                QVERIFY2(x >= std::floor(slack.left()) && x + 1 <= std::ceil(slack.right()) && y >= std::floor(slack.top())
                             && y + 1 <= std::ceil(slack.bottom()),
                         qPrintable(QStringLiteral("ink at (%1,%2) outside the text box").arg(x).arg(y)));
                if (x < box.left() + box.width() / 4)
                    ++left;
                if (x >= box.right() - box.width() / 4)
                    ++right;
                if (qRed(p) - qGreen(p) >= 60)
                    ++reddish; // red text: R 229 vs G 119 where fully inked
            }
        QVERIFY2(ink >= 30, qPrintable(QStringLiteral("%1 ink pixels").arg(ink)));
        QVERIFY(left > 0);
        QVERIFY(right > 0);
        QVERIFY(reddish > 0);
        QCOMPARE(d.addText(QPointF(5, 5), QString(), kRed, 20), -1); // empty text adds nothing
    }

    void markers()
    {
        // Circle radius 14 in accent, white bold 16 px number centred on it.
        Document d;
        d.setImage(white(200, 150));
        d.setFontFamily(QStringLiteral("Noto Sans"));
        const int m1 = d.addMarker(QPointF(100, 75), kAccent);
        const int m2 = d.addMarker(QPointF(150, 75), kAccent);
        QCOMPARE(d.annotation(m1).number, 1);
        QCOMPARE(d.annotation(m2).number, 2);
        QCOMPARE(d.annotationBounds(m1), QRectF(86, 61, 28, 28));
        const QImage r = d.render();
        // (110,75): 10.5 from the centre, inside the circle, clear of a 16 px digit (about 9 px wide, 12 tall).
        PIX(r, 110, 75, kAccent.rgb(), T);
        PIX(r, 100, 86, kAccent.rgb(), T); // 11.5 below the centre: below the digit, inside the circle
        PIX(r, 117, 75, kWhite, T);        // 17 from the centre: outside
        // The digit: near-white pixels within 7 px of each centre.
        for (const QPointF c : {QPointF(100, 75), QPointF(150, 75)}) {
            int lit = 0;
            for (int y = int(c.y()) - 7; y <= int(c.y()) + 7; ++y)
                for (int x = int(c.x()) - 7; x <= int(c.x()) + 7; ++x) {
                    const qreal dx = x + 0.5 - c.x(), dy = y + 0.5 - c.y();
                    const QRgb p = r.pixel(x, y);
                    if (dx * dx + dy * dy <= 49 && qRed(p) >= 0xf0 && qGreen(p) >= 0xf0 && qBlue(p) >= 0xf0)
                        ++lit;
                }
            QVERIFY2(lit >= 6, qPrintable(QStringLiteral("%1 digit pixels at %2,%3").arg(lit).arg(c.x()).arg(c.y())));
        }
        // "1" and "2" are different digits.
        int diff = 0;
        for (int dy = -7; dy <= 7; ++dy)
            for (int dx = -7; dx <= 7; ++dx)
                if (r.pixel(100 + dx, 75 + dy) != r.pixel(150 + dx, 75 + dy))
                    ++diff;
        QVERIFY2(diff >= 5, qPrintable(QStringLiteral("markers 1 and 2 differ in %1 pixels").arg(diff)));
        // Numbering: 1 + the highest number present.
        QVERIFY(d.removeAnnotation(m1));
        const int m3 = d.addMarker(QPointF(40, 40), kAccent);
        QCOMPARE(d.annotation(m3).number, 3);
        QVERIFY(d.removeAnnotation(m2));
        QVERIFY(d.removeAnnotation(m3));
        QCOMPARE(d.annotation(d.addMarker(QPointF(40, 40), kAccent)).number, 1);
    }

    void eraser()
    {
        Document d;
        d.setImage(white(200, 150));
        d.addStroke(Annotation::Pen, {QPointF(20, 40), QPointF(100, 40), QPointF(100, 120)}, kRed, 6);
        const QImage withStroke = d.render();
        // (60,80) is inside the L's bounding box but 40 px from both legs (hit distance 6/2 + 3 = 6): no hit.
        QVERIFY(!d.erase(QPointF(60, 80)));
        QCOMPARE(d.annotationCount(), 1);
        // (60,41) is 1 px from the first leg.
        QVERIFY(d.erase(QPointF(60, 41)));
        QCOMPARE(d.annotationCount(), 0);
        SAME(d.render(), white(200, 150)); // the stroke's pixels are gone, every one
        d.undo();
        SAME(d.render(), withStroke);

        // The Eraser tool: one press-release erasing two objects is one undo step.
        d.addLine(QPointF(20, 130), QPointF(180, 130), kRed, 4);
        const QImage both = d.render();
        d.setTool(Document::Eraser);
        d.press(QPointF(60, 41));
        d.move(QPointF(100, 130)); // on the line
        d.release(QPointF(100, 130));
        QCOMPARE(d.annotationCount(), 0);
        SAME(d.render(), white(200, 150));
        d.undo();
        QCOMPARE(d.annotationCount(), 2);
        SAME(d.render(), both);
    }

    void blur()
    {
        // Blur (40,40) 80x40 on the 8x8 checkerboard. Its edges lie on block boundaries (40 = 5*8, 120 = 15*8).
        // A box of radius 8 (17x17) holds 8 or 9 columns of each kind and 8 or 9 rows: white fraction
        // (9*9 + 8*8) / 289 = 0.502 or (9*8 + 8*9) / 289 = 0.498, about 128. A Gaussian of sigma 4 leaves a
        // checkerboard ripple of about +-17. Clamped at the rect's edges, a pixel 4 px inside sees at worst 12 of 17
        // columns of one kind: 0.7*0.7 + 0.3*0.3 = 0.58, 148. So every channel 4+ px inside lies in [48,207]
        // (black 0 and white 255 cannot survive), while a weak blur (sigma 2: ripple +-112) fails.
        const QImage original = checker(160, 120);
        Document d;
        d.setImage(original);
        const int id = d.addBlur(QRectF(40, 40, 80, 40));
        QVERIFY(id > 0);
        QCOMPARE(d.annotation(id).kind, Annotation::Blur);
        const QImage r = d.render();
        const QRect region(40, 40, 80, 40);
        int changed = 0;
        for (int y = 0; y < 120; ++y)
            for (int x = 0; x < 160; ++x) {
                const QRgb p = r.pixel(x, y);
                if (!region.contains(x, y)) {
                    QVERIFY2(p == original.pixel(x, y),
                             qPrintable(QStringLiteral("pixel (%1,%2) outside the blur changed").arg(x).arg(y)));
                    continue;
                }
                if (p != original.pixel(x, y))
                    ++changed;
                if (region.adjusted(4, 4, -4, -4).contains(x, y))
                    QVERIFY2(qRed(p) >= 48 && qRed(p) <= 207 && qGreen(p) >= 48 && qGreen(p) <= 207 && qBlue(p) >= 48
                                 && qBlue(p) <= 207,
                             qPrintable(QStringLiteral("blurred pixel (%1,%2) is %3: not blurred enough")
                                            .arg(x).arg(y).arg(hex(p))));
            }
        // No longer the original: at least 95% of the 3200 pixels differ.
        QVERIFY2(changed >= 3040, qPrintable(QStringLiteral("only %1 of 3200 pixels changed").arg(changed)));
        d.undo();
        SAME(d.render(), original);

        // It blurs what lies below it (here a red filled square), and what is drawn above it stays sharp.
        Document e;
        e.setImage(white(200, 150));
        e.addRectangle(QRectF(60, 40, 40, 40), kRed, 4, true); // red over x [58,102], y [38,82]
        e.addBlur(QRectF(40, 20, 80, 80));
        // (56,60) was white, 1 px left of the red band; red within 8 px of it now bleeds in: G 255 drops by at least
        // 10 (red's G is 119).
        QVERIFY2(qGreen(e.render().pixel(56, 60)) <= 245,
                 qPrintable(QStringLiteral("(56,60) is %1: the red below was not blurred").arg(hex(e.render().pixel(56, 60)))));
        e.addStroke(Annotation::Pen, {QPointF(50, 60), QPointF(110, 60)}, QColor(0, 0, 0), 6);
        PIX(e.render(), 80, 60, kBlack, T); // drawn over the blur: sharp
    }

    void pixelate()
    {
        // Pixelate (40,40) 80x40 on the checkerboard: 10x10 blocks from (40,40), 8 across, 4 down, each the
        // rounded mean (sum + 50) / 100.
        //   Block x 40-49, y 40-49: columns 40-47 (block 5) and 48-49 (6), rows the same. White where the block
        //     indices' sum is odd: (5,6) 8*2 + (6,5) 2*8 = 32 white pixels: 32*255 = 8160, (8160+50)/100 = 82.
        //   Block x 50-59, y 40-49: columns 50-55 (6) and 56-59 (7); rows 40-47 (5), 48-49 (6).
        //     White: (6,5) 6*8 = 48, (7,6) 4*2 = 8: 56 -> (14280+50)/100 = 143.
        //   Block x 40-49, y 50-59: the same by symmetry: 143.
        //   Block x 110-119, y 70-79: columns 110-111 (13), 112-119 (14); rows 70-71 (8), 72-79 (9).
        //     White: (13,8) 2*2 = 4, (14,9) 8*8 = 64: 68 -> (17340+50)/100 = 173.
        const QImage original = checker(160, 120);
        Document d;
        d.setImage(original);
        QVERIFY(d.addPixelate(QRectF(40, 40, 80, 40)) > 0);
        const QImage r = d.render();
        struct Block { int x, y, grey; };
        for (const Block b : {Block{40, 40, 82}, Block{50, 40, 143}, Block{40, 50, 143}, Block{110, 70, 173}})
            for (int y = b.y; y < b.y + 10; ++y)
                for (int x = b.x; x < b.x + 10; ++x)
                    PIX(r, x, y, qRgb(b.grey, b.grey, b.grey), 1);
        const QRect region(40, 40, 80, 40);
        for (int y = 0; y < 120; ++y)
            for (int x = 0; x < 160; ++x)
                if (!region.contains(x, y))
                    QVERIFY2(r.pixel(x, y) == original.pixel(x, y),
                             qPrintable(QStringLiteral("pixel (%1,%2) outside the pixelation changed").arg(x).arg(y)));
        // The Pixelate tool's drag makes the same object.
        Document e;
        e.setImage(original);
        e.setTool(Document::Pixelate);
        e.press(QPointF(40, 40));
        e.move(QPointF(90, 60));
        e.release(QPointF(120, 80));
        SAME(e.render(), r);
        e.undo();
        SAME(e.render(), original);
    }

    // --- undo and redo -----------------------------------------------------------------------------------------
    void undoRedo()
    {
        Document d;
        d.setImage(white(200, 150));
        const QImage base = d.render();
        d.addRectangle(QRectF(40, 30, 100, 60), kRed, 4, false);
        const QImage r1 = d.render();
        d.addStroke(Annotation::Pen, {QPointF(20, 120), QPointF(180, 120)}, kRed, 6);
        const QImage r2 = d.render();
        d.addBlur(QRectF(30, 20, 30, 30)); // over the rectangle's top-left corner (38..42, 28..32)
        const QImage r3 = d.render();
        QVERIFY(differing(base, r1) > 0);
        QVERIFY(differing(r1, r2) > 0);
        QVERIFY(differing(r2, r3) > 0);
        QVERIFY(d.canUndo());
        QVERIFY(!d.canRedo());
        d.undo();
        SAME(d.render(), r2);
        d.undo();
        SAME(d.render(), r1);
        d.undo();
        SAME(d.render(), base);
        QVERIFY(!d.canUndo());
        QVERIFY(d.canRedo());
        d.undo(); // nothing to undo: nothing happens
        SAME(d.render(), base);
        d.redo();
        SAME(d.render(), r1);
        d.redo();
        SAME(d.render(), r2);
        d.redo();
        SAME(d.render(), r3);
        QVERIFY(!d.canRedo());
        // A new step after undo drops the redo history.
        d.undo();
        d.undo();
        d.addEllipse(QRectF(10, 10, 30, 20), kRed, 4, false);
        QVERIFY(!d.canRedo());
        const QImage r4 = d.render();
        d.redo();
        SAME(d.render(), r4);
    }

    void undoLastStroke()
    {
        // Strokes drawn with the Pen tool: one press-move-release is one undo step, and undo removes it completely.
        Document d;
        d.setImage(white(200, 150));
        d.setTool(Document::Pen);
        d.press(QPointF(20, 20));
        d.move(QPointF(60, 20));
        d.move(QPointF(60, 60));
        QVERIFY(!d.canUndo()); // still dragging: not on the stack yet
        d.release(QPointF(60, 60));
        QVERIFY(d.canUndo());
        QCOMPARE(d.annotationCount(), 1);
        const QImage first = d.render();
        PIX(first, 40, 20, kRed.rgb(), T); // on the first leg, band y [18,22]
        d.press(QPointF(120, 100));
        d.move(QPointF(160, 100));
        d.release(QPointF(160, 130));
        QCOMPARE(d.annotationCount(), 2);
        QVERIFY(differing(first, d.render()) > 0);
        d.undo();
        QCOMPARE(d.annotationCount(), 1);
        SAME(d.render(), first);
        d.undo();
        QCOMPARE(d.annotationCount(), 0);
        SAME(d.render(), white(200, 150));
        d.redo();
        SAME(d.render(), first);
    }

    void idsStable()
    {
        Document d;
        d.setImage(white(200, 150));
        const int a = d.addRectangle(QRectF(10, 10, 20, 20), kRed, 4, false);
        const int b = d.addEllipse(QRectF(50, 50, 20, 20), kRed, 4, false);
        QCOMPARE(a, 1);
        QCOMPARE(b, 2);
        d.undo();
        d.redo();
        QCOMPARE(d.ids(), QList<int>({1, 2})); // redo brings back the same id
        d.undo();
        QCOMPARE(d.addLine(QPointF(0, 0), QPointF(9, 9), kRed, 2), 3); // never reused
        d.setImage(white(100, 100));
        QCOMPARE(d.annotationCount(), 0);
        QVERIFY(!d.canUndo());
        QCOMPARE(d.cropRect(), QRect(0, 0, 100, 100));
        QCOMPARE(d.addLine(QPointF(0, 0), QPointF(9, 9), kRed, 2), 1); // a new image starts at 1
    }

    // --- objects stay objects ----------------------------------------------------------------------------------
    void moveResizeDelete()
    {
        Document d;
        d.setImage(white(200, 150));
        const int id = d.addRectangle(QRectF(40, 30, 100, 60), kRed, 4, false);
        const QImage original = d.render();
        // Move by (10,20): the rect becomes (50,50) 100x60, edges x 50 [48,52], x 150 [148,152], y 50, y 110.
        QVERIFY(d.moveAnnotation(id, QPointF(10, 20)));
        QCOMPARE(d.annotationBounds(id), QRectF(50, 50, 100, 60));
        QImage r = d.render();
        PIX(r, 50, 80, kRed.rgb(), T);
        PIX(r, 149, 80, kRed.rgb(), T);
        PIX(r, 90, 110, kRed.rgb(), T);
        PIX(r, 40, 60, kWhite, T);  // the old left edge: gone
        PIX(r, 139, 60, kWhite, T); // the old right edge: now inside the moved rect
        PIX(r, 90, 30, kWhite, T);  // the old top edge: gone
        {
            Document fresh;
            fresh.setImage(white(200, 150));
            fresh.addRectangle(QRectF(50, 50, 100, 60), kRed, 4, false);
            SAME(r, fresh.render()); // exactly as if drawn there
        }
        d.undo();
        SAME(d.render(), original);
        // Resize to (40,30) 60x40: right edge x 100 [98,102], bottom y 70 [68,72].
        QVERIFY(d.setAnnotationRect(id, QRectF(40, 30, 60, 40)));
        r = d.render();
        PIX(r, 100, 50, kRed.rgb(), T);
        PIX(r, 70, 70, kRed.rgb(), T);
        PIX(r, 139, 60, kWhite, T); // the old right edge: gone
        PIX(r, 90, 89, kWhite, T);  // the old bottom edge: gone
        {
            Document fresh;
            fresh.setImage(white(200, 150));
            fresh.addRectangle(QRectF(40, 30, 60, 40), kRed, 4, false);
            SAME(r, fresh.render());
        }
        // Delete.
        QVERIFY(d.removeAnnotation(id));
        SAME(d.render(), white(200, 150));
        d.undo();
        SAME(d.render(), r);
        // Nothing to change: false, no undo step.
        QVERIFY(!d.moveAnnotation(id, QPointF(0, 0)));
        QVERIFY(!d.moveAnnotation(999, QPointF(5, 5)));
        QVERIFY(!d.removeAnnotation(999));

        // A stroke resized: points mapped from bounds (20,40) 80x80 to (20,40) 40x40:
        // (20,40) -> (20,40); (100,40) -> (20 + 80*0.5, 40) = (60,40); (100,120) -> (60, 40 + 80*0.5) = (60,80).
        const int s = d.addStroke(Annotation::Pen, {QPointF(20, 40), QPointF(100, 40), QPointF(100, 120)}, kRed, 6);
        QVERIFY(d.setAnnotationRect(s, QRectF(20, 40, 40, 40)));
        QCOMPARE(d.annotation(s).points, QList<QPointF>({QPointF(20, 40), QPointF(60, 40), QPointF(60, 80)}));
        // A horizontal line has bounds of height 0: y' = y + new.y - old.y. (20,75)-(180,75), bounds (20,75) 160x0,
        // to (20,85) 80x0: (20,85), (100,85).
        const int l = d.addLine(QPointF(20, 75), QPointF(180, 75), kRed, 4);
        QVERIFY(d.setAnnotationRect(l, QRectF(20, 85, 80, 0)));
        QCOMPARE(d.annotation(l).points, QList<QPointF>({QPointF(20, 85), QPointF(100, 85)}));
        QVERIFY(d.moveAnnotation(l, QPointF(0, 10)));
        QCOMPARE(d.annotation(l).points, QList<QPointF>({QPointF(20, 95), QPointF(100, 95)}));
        // Text and markers move but do not resize.
        const int t = d.addText(QPointF(30, 40), QStringLiteral("Hi"), kRed, 20);
        QVERIFY(!d.setAnnotationRect(t, QRectF(0, 0, 50, 50)));
        QVERIFY(d.moveAnnotation(t, QPointF(5, 5)));
        QCOMPARE(d.annotation(t).position, QPointF(35, 45));
        const int m = d.addMarker(QPointF(100, 75), kAccent);
        QVERIFY(!d.setAnnotationRect(m, QRectF(0, 0, 50, 50)));
        QVERIFY(d.moveAnnotation(m, QPointF(-10, 5)));
        QCOMPARE(d.annotation(m).position, QPointF(90, 80));
    }

    void selectTool()
    {
        Document d;
        d.setImage(white(200, 150));
        const int id = d.addRectangle(QRectF(40, 30, 100, 60), kRed, 4, false);
        const QImage original = d.render();
        d.setTool(Document::Select);
        // Press on the left edge selects it; dragging by (10,20) moves it: one undo step.
        d.press(QPointF(40, 60));
        QCOMPARE(d.selectedId(), id);
        d.move(QPointF(45, 70));
        d.move(QPointF(50, 80));
        d.release(QPointF(50, 80));
        QCOMPARE(d.annotationBounds(id), QRectF(50, 50, 100, 60));
        QCOMPARE(d.selectedBounds(), QRectF(50, 50, 100, 60));
        d.undo();
        QCOMPARE(d.annotationBounds(id), QRectF(40, 30, 100, 60));
        SAME(d.render(), original);
        // The bottom-right handle (140,90): drag it to (100,70): (40,30) 60x40.
        d.setSelectedId(id);
        d.press(QPointF(140, 90));
        d.move(QPointF(100, 70));
        d.release(QPointF(100, 70));
        QCOMPARE(d.annotationBounds(id), QRectF(40, 30, 60, 40));
        d.undo();
        // The right edge's handle (140,60): drag it to (160,60): only the right edge moves: (40,30) 120x60.
        d.setSelectedId(id);
        d.press(QPointF(140, 60));
        d.release(QPointF(160, 60));
        QCOMPARE(d.annotationBounds(id), QRectF(40, 30, 120, 60));
        d.undo();
        // A press and release in place changes nothing and adds no undo step.
        while (d.canUndo())
            d.undo();
        d.press(QPointF(90, 30));
        d.release(QPointF(90, 30));
        QCOMPARE(d.selectedId(), -1); // the rectangle's add was undone too: nothing to select
        QVERIFY(!d.canUndo());
        QVERIFY(d.canRedo()); // a step would have dropped the redo history
        d.redo();             // the rectangle is back (the right-edge resize can still be redone)
        d.press(QPointF(90, 30));
        d.release(QPointF(90, 30));
        QCOMPARE(d.selectedId(), id);
        QVERIFY(d.canRedo()); // selecting in place added no step
        QCOMPARE(d.annotationBounds(id), QRectF(40, 30, 100, 60));
        // Press on nothing deselects.
        d.press(QPointF(190, 140));
        d.release(QPointF(190, 140));
        QCOMPARE(d.selectedId(), -1);
        // Delete the selection.
        d.setSelectedId(id);
        QVERIFY(d.removeSelected());
        QCOMPARE(d.annotationCount(), 0);
        QCOMPARE(d.selectedId(), -1);
        // A marker's handle moves it rather than resizing: press at its box's corner (86,61), drag by (10,10).
        const int m = d.addMarker(QPointF(100, 75), kAccent);
        d.setSelectedId(m);
        d.press(QPointF(86, 61));
        d.release(QPointF(96, 71));
        QCOMPARE(d.annotation(m).position, QPointF(110, 85));
    }

    void selectionNotRendered()
    {
        Document d;
        d.setImage(white(200, 150));
        const int id = d.addRectangle(QRectF(40, 30, 100, 60), kRed, 4, false);
        const QImage r = d.render();
        d.setSelectedId(id);
        QCOMPARE(d.selectedId(), id);
        QCOMPARE(d.selectedBounds(), QRectF(40, 30, 100, 60));
        SAME(d.render(), r); // no outline, no handles in the output
        d.setSelectedId(77);
        QCOMPARE(d.selectedId(), -1);
    }

    // --- the tools, driven like the canvas drives them ------------------------------------------------------------
    void toolsByPointer()
    {
        auto reference = [](auto add) {
            Document e;
            e.setImage(white(200, 150));
            add(e);
            return e.render();
        };
        Document d;
        d.setImage(white(200, 150));
        d.setTool(Document::Rectangle);
        d.press(QPointF(40, 30));
        d.move(QPointF(90, 60));
        d.release(QPointF(140, 90));
        SAME(d.render(), reference([](Document &e) { e.addRectangle(QRectF(40, 30, 100, 60), kRed, 4, false); }));
        // Dragged up-left: the same rect.
        d.undo();
        d.press(QPointF(140, 90));
        d.release(QPointF(40, 30));
        SAME(d.render(), reference([](Document &e) { e.addRectangle(QRectF(40, 30, 100, 60), kRed, 4, false); }));
        d.undo();
        // A click without a drag adds nothing.
        d.press(QPointF(50, 50));
        d.release(QPointF(50, 50));
        QCOMPARE(d.annotationCount(), 0);
        // Filled ellipse.
        d.setTool(Document::Ellipse);
        d.setFilled(true);
        d.press(QPointF(40, 30));
        d.release(QPointF(160, 110));
        SAME(d.render(), reference([](Document &e) { e.addEllipse(QRectF(40, 30, 120, 80), kRed, 4, true); }));
        d.undo();
        d.setFilled(false);
        // Arrow with width 6.
        d.setTool(Document::Arrow);
        d.setStrokeWidth(6);
        d.press(QPointF(20, 75));
        d.move(QPointF(100, 75));
        d.release(QPointF(180, 75));
        SAME(d.render(), reference([](Document &e) { e.addArrow(QPointF(20, 75), QPointF(180, 75), kRed, 6); }));
        d.undo();
        // Line in a chosen colour.
        d.setTool(Document::Line);
        d.setColour(kGreen);
        d.press(QPointF(20, 20));
        d.release(QPointF(180, 130));
        SAME(d.render(), reference([](Document &e) { e.addLine(QPointF(20, 20), QPointF(180, 130), kGreen, 6); }));
        d.undo();
        // Highlighter: its own default colour, yellow.
        d.setTool(Document::Highlighter);
        d.setStrokeWidth(10);
        d.press(QPointF(20, 50));
        d.move(QPointF(80, 50));
        d.release(QPointF(80, 50));
        PIX(d.render(), 35, 50, qRgb(241, 224, 180), 2); // see highlighter()
        d.undo();
        // Marker: a press adds it, in the marker colour.
        d.setTool(Document::Marker);
        d.press(QPointF(100, 75));
        d.release(QPointF(100, 75));
        QCOMPARE(d.annotationCount(), 1);
        const Annotation m = d.annotation(d.ids().first());
        QCOMPARE(m.kind, Annotation::Marker);
        QCOMPARE(m.position, QPointF(100, 75));
        QCOMPARE(m.number, 1);
        QCOMPARE(m.colour.rgb(), kAccent.rgb());
        d.undo();
        // Text: the press asks for the text; placeText adds it with the Text tool's colour and textSize.
        d.setTool(Document::Text);
        d.setColour(kMauve);
        d.setTextSize(30);
        QSignalSpy asked(&d, &Document::textRequested);
        d.press(QPointF(30, 40));
        d.release(QPointF(30, 40));
        QCOMPARE(asked.count(), 1);
        QCOMPARE(asked.first().first().toPointF(), QPointF(30, 40));
        QCOMPARE(d.annotationCount(), 0);
        const int t = d.placeText(QPointF(30, 40), QStringLiteral("Hi"));
        QVERIFY(t > 0);
        QCOMPARE(d.annotation(t).text, QStringLiteral("Hi"));
        QCOMPARE(d.annotation(t).colour.rgb(), kMauve.rgb());
        QCOMPARE(d.annotation(t).textSize, 30.0);
        QCOMPARE(d.annotation(t).position, QPointF(30, 40));
        // Blur by drag equals addBlur.
        Document b;
        b.setImage(checker(160, 120));
        b.setTool(Document::Blur);
        b.press(QPointF(40, 40));
        b.release(QPointF(120, 80));
        Document b2;
        b2.setImage(checker(160, 120));
        b2.addBlur(QRectF(40, 40, 80, 40));
        SAME(b.render(), b2.render());
    }

    // --- crop ----------------------------------------------------------------------------------------------------
    void crop()
    {
        // gradient: pixel (x,y) = rgb(x,y,0), so every output pixel tells which image pixel it came from.
        Document d;
        d.setImage(gradient(200, 150));
        d.addRectangle(QRectF(40, 30, 100, 60), kRed, 4, false);
        QVERIFY(d.crop(QRect(20, 10, 100, 60)));
        QCOMPARE(d.cropRect(), QRect(20, 10, 100, 60));
        QCOMPARE(d.outputSize(), QSize(100, 60));
        QCOMPARE(d.imageSize(), QSize(200, 150)); // the shot itself is kept
        QImage r = d.render();
        QCOMPARE(r.size(), QSize(100, 60));
        PIX(r, 0, 0, qRgb(20, 10, 0), 0);    // image (20,10)
        PIX(r, 99, 59, qRgb(119, 69, 0), 0); // image (119,69): inside the rectangle, not on an edge
        PIX(r, 10, 15, qRgb(30, 25, 0), 0);  // image (30,25): outside the rectangle
        PIX(r, 20, 50, kRed.rgb(), T);       // image (40,60): the rectangle's left edge, moved with the crop
        // Undo restores the full size.
        d.undo();
        QCOMPARE(d.outputSize(), QSize(200, 150));
        QCOMPARE(d.render().size(), QSize(200, 150));
        // Clipped to the image: (150,100) 100x100 -> (150,100) 50x50.
        QVERIFY(d.crop(QRect(150, 100, 100, 100)));
        QCOMPARE(d.cropRect(), QRect(150, 100, 50, 50));
        // A second crop is in image coordinates too, not the current crop's.
        QVERIFY(d.crop(QRect(30, 20, 10, 10)));
        QCOMPARE(d.cropRect(), QRect(30, 20, 10, 10));
        PIX(d.render(), 0, 0, qRgb(30, 20, 0), 0);
        // Wholly outside: nothing changes.
        QVERIFY(!d.crop(QRect(300, 300, 10, 10)));
        QCOMPARE(d.cropRect(), QRect(30, 20, 10, 10));
        // The Crop tool: a drag from (20,10) to (120,70).
        Document e;
        e.setImage(gradient(200, 150));
        e.setTool(Document::Crop);
        e.press(QPointF(20, 10));
        e.move(QPointF(60, 40));
        e.release(QPointF(120, 70));
        QCOMPARE(e.cropRect(), QRect(20, 10, 100, 60));
        QCOMPARE(e.render().size(), QSize(100, 60));
    }

    // --- save and copy -------------------------------------------------------------------------------------------
    void save()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        Document d;
        d.setImage(checker(160, 120));
        d.addRectangle(QRectF(20, 20, 60, 40), kRed, 4, true);
        d.addPixelate(QRectF(90, 20, 50, 50));
        d.addArrow(QPointF(10, 110), QPointF(150, 100), kGreen, 4);
        const QImage r = d.render();
        const int count = d.annotationCount();
        // PNG: every pixel equals the render, pixelation baked in.
        const QString png = tmp.filePath(QStringLiteral("a.png"));
        QVERIFY(d.saveAs(png));
        QCOMPARE(d.lastSavedPath(), png);
        QVERIFY(readAll(png).startsWith("\x89PNG\r\n\x1a\n"));
        SAME(QImage(png).convertToFormat(QImage::Format_RGB32), r);
        // A file:// URL.
        const QString viaUrl = tmp.filePath(QStringLiteral("b.png"));
        QVERIFY(d.saveAs(QUrl::fromLocalFile(viaUrl).toString()));
        SAME(QImage(viaUrl).convertToFormat(QImage::Format_RGB32), r);
        // After a crop the file has the crop's size.
        QVERIFY(d.crop(QRect(10, 10, 100, 80)));
        const QString cropped = tmp.filePath(QStringLiteral("c.png"));
        QVERIFY(d.saveAs(cropped));
        QCOMPARE(QImage(cropped).size(), QSize(100, 80));
        SAME(QImage(cropped).convertToFormat(QImage::Format_RGB32), d.render());
        d.undo();
        // Saving changed nothing.
        QCOMPARE(d.annotationCount(), count);
        SAME(d.render(), r);
        QVERIFY(!d.saveAs(QStringLiteral("/proc/no-such-folder/x.png")));
        QVERIFY(!d.lastError().isEmpty());
    }

    void saveJpeg()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        Document d;
        d.setImage(white(200, 150));
        d.addRectangle(QRectF(40, 30, 100, 60), kRed, 4, true);
        const QImage r = d.render();
        for (const char *name : {"d.jpg", "e.JPEG"}) {
            const QString path = tmp.filePath(QLatin1String(name));
            QVERIFY(d.saveAs(path));
            QVERIFY2(readAll(path).startsWith("\xff\xd8\xff"), name); // JPEG's start-of-image marker
            const QImage back = QImage(path).convertToFormat(QImage::Format_RGB32);
            QCOMPARE(back.size(), QSize(200, 150));
            // Quality 92 on flat colour: small mean error; deep inside the red the colour holds.
            qint64 err = 0;
            for (int y = 0; y < 150; ++y)
                for (int x = 0; x < 200; ++x) {
                    const QRgb a = back.pixel(x, y), b = r.pixel(x, y);
                    err += qAbs(qRed(a) - qRed(b)) + qAbs(qGreen(a) - qGreen(b)) + qAbs(qBlue(a) - qBlue(b));
                }
            QVERIFY2(err <= 3 * 3 * 200 * 150, qPrintable(QStringLiteral("mean error %1").arg(err / (3.0 * 200 * 150))));
            PIX(back, 90, 60, kRed.rgb(), 8);
            PIX(back, 10, 10, kWhite, 8);
        }
    }

    void quickSave()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        Document d;
        d.setImage(white(200, 150));
        d.addRectangle(QRectF(40, 30, 100, 60), kRed, 4, false);
        const QString folder = tmp.filePath(QStringLiteral("shots")); // does not exist yet
        d.setSaveFolder(folder);
        d.setFileNamePattern(QStringLiteral("Shot_%Y-%m-%d_%H-%M-%S"));
        d.setNow(QDateTime(QDate(2026, 11, 12), QTime(21, 5, 9)));
        QSignalSpy saved(&d, &Document::saved);
        const QString first = d.quickSave();
        QCOMPARE(first, folder + QStringLiteral("/Shot_2026-11-12_21-05-09.png"));
        QCOMPARE(saved.count(), 1);
        SAME(QImage(first).convertToFormat(QImage::Format_RGB32), d.render());
        // Never overwrites: deliver.h's "-2".
        QCOMPARE(d.quickSave(QStringLiteral("png")), folder + QStringLiteral("/Shot_2026-11-12_21-05-09-2.png"));
        const QString jpg = d.quickSave(QStringLiteral("jpg"));
        QCOMPARE(jpg, folder + QStringLiteral("/Shot_2026-11-12_21-05-09.jpg"));
        QVERIFY(readAll(jpg).startsWith("\xff\xd8\xff"));
        QCOMPARE(d.lastSavedPath(), jpg);
    }

    void copy()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        qputenv("FAKE_WLCOPY_DIR", tmp.path().toLocal8Bit());
        qunsetenv("FAKE_WLCOPY_EXIT");
        Document d;
        d.setImage(checker(160, 120));
        d.addBlur(QRectF(40, 40, 80, 40));
        d.addMarker(QPointF(20, 20), kAccent);
        d.setCopyCommand({QStringLiteral(RILEYSHOT_SOURCE_DIR "/tests/fake/wl-copy"), QStringLiteral("--type"),
                          QStringLiteral("image/png")});
        QVERIFY(d.copyToClipboard());
        QCOMPARE(readAll(tmp.filePath(QStringLiteral("args"))), QByteArray("--type\nimage/png\n"));
        const QByteArray bytes = readAll(tmp.filePath(QStringLiteral("stdin")));
        QVERIFY(bytes.startsWith("\x89PNG\r\n\x1a\n"));
        SAME(QImage::fromData(bytes, "PNG").convertToFormat(QImage::Format_RGB32), d.render());
        QVERIFY(d.lastError().isEmpty());
        // The clipboard program fails: false, and why.
        qputenv("FAKE_WLCOPY_EXIT", "1");
        QVERIFY(!d.copyToClipboard());
        QVERIFY(!d.lastError().isEmpty());
        qunsetenv("FAKE_WLCOPY_EXIT");
    }
};

QTEST_MAIN(TestDocument)
#include "tst_document.moc"
