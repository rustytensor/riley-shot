// SPDX-License-Identifier: GPL-3.0-or-later
#include "overlaysurface.h"

#include "capturecontroller.h"

#include <LayerShellQt/Window>

#include <QColor>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>

#include <cmath>

namespace rileyshot {

OverlaySurface::OverlaySurface(QQmlEngine *engine, CaptureController *controller, const Output &output,
                               QScreen *screen, QObject *parent)
    : QObject(parent), m_output(output)
{
    m_view = new QQuickView(engine, nullptr);
    m_view->setFlags(Qt::FramelessWindowHint);
    m_view->setColor(Qt::black);
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    if (screen)
        m_view->setScreen(screen);

    auto *layer = LayerShellQt::Window::get(m_view);
    layer->setScope(QStringLiteral("riley-shot"));
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    if (screen)
        layer->setScreen(screen);
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop)
                      | LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorLeft
                      | LayerShellQt::Window::AnchorRight);
    layer->setExclusiveZone(-1);
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
    layer->setCloseOnDismissed(false);

    // The output's logical size; the compositor sizes an all-anchored layer to the output anyway.
    m_view->resize(output.logicalSize());

    m_view->setInitialProperties({
        {QStringLiteral("controller"), QVariant::fromValue(controller)},
        {QStringLiteral("outputName"), output.name},
        {QStringLiteral("layoutX"), output.layoutPos.x()},
        {QStringLiteral("layoutY"), output.layoutPos.y()},
        {QStringLiteral("deviceWidth"), output.deviceSize().width()},
        {QStringLiteral("deviceHeight"), output.deviceSize().height()},
        {QStringLiteral("outputScale"), output.scale},
    });
    m_view->loadFromModule("RileyShot", "Overlay");
    if (m_view->status() != QQuickView::Ready || !m_view->rootObject()) {
        QStringList lines;
        for (const QQmlError &e : m_view->errors())
            lines << e.toString();
        m_error = lines.isEmpty() ? QStringLiteral("Overlay.qml did not load") : lines.join(QLatin1Char('\n'));
    }

    // frameSwapped comes from the render thread; handle it on ours.
    connect(m_view, &QQuickWindow::frameSwapped, this, [this] {
        if (m_framed)
            return;
        m_framed = true;
        emit firstFrame();
    }, Qt::QueuedConnection);
}

OverlaySurface::~OverlaySurface()
{
    delete m_view;
}

bool OverlaySurface::isValid() const
{
    return m_error.isEmpty();
}

QString OverlaySurface::errorString() const
{
    return m_error;
}

QString OverlaySurface::outputName() const
{
    return m_output.name;
}

QQuickView *OverlaySurface::view() const
{
    return m_view;
}

void OverlaySurface::show()
{
    m_view->show();
    m_view->requestActivate();
    if (QQuickItem *root = m_view->rootObject())
        root->forceActiveFocus();
}

void OverlaySurface::hide()
{
    m_view->hide();
}

double OverlaySurface::devicePixelRatio() const
{
    return m_view->devicePixelRatio();
}

QSize OverlaySurface::deviceSize() const
{
    const double dpr = devicePixelRatio();
    return QSize(qRound(m_view->width() * dpr), qRound(m_view->height() * dpr));
}

QQuickItem *OverlaySurface::captureBar() const
{
    QQuickItem *root = m_view->rootObject();
    return root ? root->findChild<QQuickItem *>(QStringLiteral("captureBar")) : nullptr;
}

bool OverlaySurface::hasVisibleBar() const
{
    const QQuickItem *bar = captureBar();
    return bar && bar->isVisible();
}

QRect OverlaySurface::barDeviceRect() const
{
    QQuickItem *bar = captureBar();
    if (!bar || !bar->isVisible())
        return QRect();
    const QRectF scene = bar->mapRectToScene(QRectF(0, 0, bar->width(), bar->height()));
    const double dpr = devicePixelRatio();
    const int l = int(std::floor(scene.left() * dpr + kEpsilon));
    const int t = int(std::floor(scene.top() * dpr + kEpsilon));
    const int r = int(std::ceil(scene.right() * dpr - kEpsilon));
    const int b = int(std::ceil(scene.bottom() * dpr - kEpsilon));
    return QRect(QPoint(l, t), QSize(r - l, b - t));
}

} // namespace rileyshot
