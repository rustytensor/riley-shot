// SPDX-License-Identifier: GPL-3.0-or-later
// rileyshot-testpattern: the M1 gate's test window. A full-output layer-shell overlay on one output (DP-2 in the gate)
// that draws pattern.h at 1:1 device pixels, takes no input and no keyboard, and closes by itself.
//
//   rileyshot-testpattern --output DP-2 [--close-after MS]
//
// Prints one line on stdout once the first frame is committed:
//   READY buffer=2560x1600 dpr=1.25
// or, when Qt did not give the surface a buffer of exactly the pattern's device size (so it cannot draw 1:1):
//   BADBUFFER buffer=WxH dpr=D
// and exits 3. It exits 0 when its timer runs out (default 8000 ms, never more than 9000) or on SIGTERM.
#include "pattern.h"

#include <LayerShellQt/Window>

#include <QBackingStore>
#include <QCommandLineParser>
#include <QExposeEvent>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#include <csignal>
#include <cmath>
#include <cstdio>

namespace {

class PatternWindow : public QWindow
{
public:
    explicit PatternWindow(QScreen *screen) : m_store(this)
    {
        setFlags(Qt::FramelessWindowHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
        setScreen(screen);
        auto *layer = LayerShellQt::Window::get(this);
        layer->setScope(QStringLiteral("riley-shot-testpattern"));
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setScreen(screen);
        layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop)
                          | LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorLeft
                          | LayerShellQt::Window::AnchorRight);
        layer->setExclusiveZone(-1);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setActivateOnShow(false);
        layer->setCloseOnDismissed(false);
        resize(screen->geometry().size());
    }

protected:
    void exposeEvent(QExposeEvent *) override
    {
        if (!isExposed() || m_done)
            return;
        const double dpr = devicePixelRatio();
        const QSize buffer(qRound(width() * dpr), qRound(height() * dpr));
        if (buffer != QSize(pattern::kWidth, pattern::kHeight)) {
            std::printf("BADBUFFER buffer=%dx%d dpr=%g\n", buffer.width(), buffer.height(), dpr);
            std::fflush(stdout);
            QCoreApplication::exit(3);
            m_done = true;
            return;
        }
        const QRect rect(QPoint(0, 0), size());
        m_store.resize(size());
        m_store.beginPaint(rect);
        {
            QPainter painter(m_store.paintDevice());
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            QImage image = pattern::render();
            image.setDevicePixelRatio(dpr); // the image's device pixels land on the buffer's device pixels 1:1
            painter.drawImage(QPointF(0, 0), image);
        }
        m_store.endPaint();
        m_store.flush(rect);
        m_done = true;
        std::printf("READY buffer=%dx%d dpr=%g\n", buffer.width(), buffer.height(), dpr);
        std::fflush(stdout);
    }

private:
    QBackingStore m_store;
    bool m_done = false;
};

void onTerm(int) { QCoreApplication::quit(); }

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addOption({QStringLiteral("output"), QStringLiteral("Output name."), QStringLiteral("name")});
    parser.addOption({QStringLiteral("close-after"), QStringLiteral("Milliseconds before closing."),
                      QStringLiteral("ms"), QStringLiteral("8000")});
    parser.process(app);

    QScreen *screen = nullptr;
    for (QScreen *s : QGuiApplication::screens())
        if (s->name() == parser.value(QStringLiteral("output")))
            screen = s;
    if (!screen) {
        std::fprintf(stderr, "rileyshot-testpattern: no output named %s\n",
                     qPrintable(parser.value(QStringLiteral("output"))));
        return 2;
    }
    // The 10-second rule: the window never stays up longer than 9 s, whatever the caller asks.
    const int closeAfter = std::min(parser.value(QStringLiteral("close-after")).toInt(), 9000);
    QTimer::singleShot(closeAfter > 0 ? closeAfter : 8000, &app, &QCoreApplication::quit);
    std::signal(SIGTERM, onTerm);
    std::signal(SIGINT, onTerm);

    PatternWindow window(screen);
    window.show();
    return app.exec();
}
