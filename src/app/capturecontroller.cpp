// SPDX-License-Identifier: GPL-3.0-or-later
#include "capturecontroller.h"

#include "compose.h"
#include "deliver.h"

#include <QColor>
#include <QDir>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickImageProvider>

#include <cmath>

namespace rileyshot {

namespace {

// Layout coordinates from QML are real; RegionSelection takes whole layout pixels. Floored with the project's
// rounding guard (output.h), so 159.9999999 from a scale division still lands on 160.
QPoint floored(qreal x, qreal y)
{
    return QPoint(int(std::floor(x + kEpsilon)), int(std::floor(y + kEpsilon)));
}

// image://frozen/<output>: the output's frozen grab at its device size, never scaled, so Overlay.qml can show it
// device pixel for device pixel.
class FrozenImageProvider : public QQuickImageProvider
{
public:
    explicit FrozenImageProvider(CaptureController *controller)
        : QQuickImageProvider(QQuickImageProvider::Image), m_controller(controller)
    {
    }

    QImage requestImage(const QString &id, QSize *size, const QSize &) override
    {
        const QImage image = m_controller ? m_controller->image(id) : QImage();
        if (size)
            *size = image.size();
        return image;
    }

private:
    QPointer<CaptureController> m_controller;
};

} // namespace

struct CaptureController::Private
{
    QList<Output> outputs;
    QHash<QString, QImage> images;
    QHash<QString, QImage> pointerImages;
    QList<Client> clients;
    Config config;
    QVariantMap theme;
    QStringList copyCommand = copyCommandFromEnvironment();
    QString home = QDir::homePath();
    QDateTime now;
    QPointF cursor;

    int mode = Region;
    bool pointer = false;
    bool barVisible = false;
    QString barOutput;

    RegionSelection selection;
    QString hoverOutput;
    int hoverWindow = -1; // index in clients

    // The press of a click in Screen and Window mode.
    bool pressed = false;
    QString pressOutput;
    int pressWindow = -1;

    bool canCapture = false;
    bool finished = false;
    QImage result;
    RegionPlan resultPlan;
    QString savedPath;
    QString error;
};

CaptureController::CaptureController(QObject *parent) : QObject(parent), d(new Private)
{
    setTheme(Theme());
}

CaptureController::~CaptureController()
{
    delete d;
}

// --- helpers -------------------------------------------------------------------------------------------------------

namespace {

QString outputNameAt(const QList<Output> &outputs, QPoint p)
{
    const Output *o = outputAt(outputs, QPointF(p));
    return o ? o->name : QString();
}

} // namespace

// Recomputes canCapture and emits its signal when it changed.
static void refreshCanCapture(CaptureController *c, bool &cached, bool now)
{
    if (cached != now) {
        cached = now;
        emit c->canCaptureChanged();
    }
}

// --- set up ---------------------------------------------------------------------------------------------------------

void CaptureController::setOutputs(const QList<Output> &outputs)
{
    d->outputs = outputs;
    d->selection.setBounds(layoutBounds(outputs));
    d->selection.setRect(d->selection.rect()); // keep any region inside the new bounds
    const QString bar = barOutputFor(outputs, d->cursor);
    if (bar != d->barOutput) {
        d->barOutput = bar;
        emit barOutputChanged();
    }
    emit selectionChanged();
    refreshCanCapture(this, d->canCapture, canCapture());
}

QList<Output> CaptureController::outputs() const
{
    return d->outputs;
}

void CaptureController::setImages(const QHash<QString, QImage> &images, const QHash<QString, QImage> &pointerImages)
{
    d->images = images;
    d->pointerImages = pointerImages;
}

QImage CaptureController::image(const QString &output) const
{
    return d->images.value(output);
}

void CaptureController::setClients(const QList<Client> &clients)
{
    d->clients = clients;
    if (d->hoverWindow >= 0) {
        d->hoverWindow = -1;
        emit hoverChanged();
    }
    d->pressWindow = -1;
    refreshCanCapture(this, d->canCapture, canCapture());
}

void CaptureController::setConfig(const Config &config)
{
    d->config = config;
    setPointer(config.pointer);
}

void CaptureController::setTheme(const Theme &theme)
{
    QVariantMap map;
    for (const QString &key : Theme::keys())
        map.insert(key, QVariant::fromValue(theme.colour(key)));
    map.insert(QStringLiteral("font"), theme.font);
    d->theme = map;
    emit themeChanged();
}

void CaptureController::setStartMode(Mode mode)
{
    const bool bar = mode == Mode::Bar;
    int m = Region;
    if (mode == Mode::Screen)
        m = Screen;
    else if (mode == Mode::Window)
        m = Window;
    setMode(m);
    if (bar != d->barVisible) {
        d->barVisible = bar;
        emit barVisibleChanged();
    }
}

void CaptureController::setCursor(QPointF layout)
{
    d->cursor = layout;
    const QString bar = barOutputFor(d->outputs, layout);
    if (bar != d->barOutput) {
        d->barOutput = bar;
        emit barOutputChanged();
    }
}

void CaptureController::setCopyCommand(const QStringList &command)
{
    d->copyCommand = command;
}

void CaptureController::setHome(const QString &home)
{
    d->home = home;
}

void CaptureController::setNow(const QDateTime &now)
{
    d->now = now;
}

void CaptureController::installImageProvider(QQmlEngine *engine)
{
    engine->addImageProvider(QStringLiteral("frozen"), new FrozenImageProvider(this));
}

// --- read by QML -----------------------------------------------------------------------------------------------------

int CaptureController::mode() const
{
    return d->mode;
}

void CaptureController::setMode(int mode)
{
    if (mode < Region || mode > Window || mode == d->mode)
        return;
    d->mode = mode;

    // End any drag where it is: a fresh selection holding the current region.
    if (d->selection.dragging()) {
        const QRect keep = d->selection.rect();
        d->selection = RegionSelection();
        d->selection.setBounds(layoutBounds(d->outputs));
        d->selection.setRect(keep);
        emit selectionChanged();
    }
    d->pressed = false;
    d->pressOutput.clear();
    d->pressWindow = -1;
    if (!d->hoverOutput.isEmpty() || d->hoverWindow >= 0) {
        d->hoverOutput.clear();
        d->hoverWindow = -1;
        emit hoverChanged();
    }
    emit modeChanged();
    refreshCanCapture(this, d->canCapture, canCapture());
}

bool CaptureController::pointer() const
{
    return d->pointer;
}

void CaptureController::setPointer(bool on)
{
    if (on == d->pointer)
        return;
    d->pointer = on;
    emit pointerChanged();
}

bool CaptureController::barVisible() const
{
    return d->barVisible;
}

QString CaptureController::barOutput() const
{
    return d->barOutput;
}

QRect CaptureController::selection() const
{
    return d->selection.rect();
}

bool CaptureController::hasSelection() const
{
    return !d->selection.isEmpty();
}

QString CaptureController::sizeText() const
{
    return rileyshot::sizeText(d->outputs, d->selection.rect());
}

QString CaptureController::hoverOutput() const
{
    return d->hoverOutput;
}

QRect CaptureController::hoverWindow() const
{
    if (d->hoverWindow < 0 || d->hoverWindow >= d->clients.size())
        return QRect();
    return d->clients[d->hoverWindow].rect;
}

bool CaptureController::canCapture() const
{
    switch (d->mode) {
    case Region:
        return hasSelection();
    case Screen:
        return !d->outputs.isEmpty();
    case Window:
        return d->hoverWindow >= 0 && d->hoverWindow < d->clients.size();
    }
    return false;
}

QVariantMap CaptureController::theme() const
{
    return d->theme;
}

// --- called by QML ---------------------------------------------------------------------------------------------------

namespace {

// Runs a change to the selection and emits selectionChanged when the region moved.
template<typename F>
void changeSelection(CaptureController *c, RegionSelection &s, F &&f)
{
    const QRect before = s.rect();
    f();
    if (s.rect() != before)
        emit c->selectionChanged();
}

} // namespace

void CaptureController::pointerPressed(qreal x, qreal y, int modifiers)
{
    Q_UNUSED(modifiers);
    if (d->finished)
        return;
    const QPoint p = floored(x, y);
    switch (d->mode) {
    case Region:
        changeSelection(this, d->selection, [&] { d->selection.press(p); });
        break;
    case Screen:
        pointerMoved(x, y, modifiers);
        d->pressed = true;
        d->pressOutput = d->hoverOutput;
        break;
    case Window:
        pointerMoved(x, y, modifiers);
        d->pressed = true;
        d->pressWindow = d->hoverWindow;
        break;
    }
    refreshCanCapture(this, d->canCapture, canCapture());
}

void CaptureController::pointerMoved(qreal x, qreal y, int modifiers)
{
    Q_UNUSED(modifiers);
    if (d->finished)
        return;
    const QPoint p = floored(x, y);
    switch (d->mode) {
    case Region:
        if (d->selection.dragging())
            changeSelection(this, d->selection, [&] { d->selection.move(p); });
        break;
    case Screen: {
        const QString name = outputNameAt(d->outputs, p);
        if (name != d->hoverOutput) {
            d->hoverOutput = name;
            emit hoverChanged();
        }
        break;
    }
    case Window: {
        const int index = clientAt(d->clients, d->outputs, p);
        if (index != d->hoverWindow) {
            d->hoverWindow = index;
            emit hoverChanged();
        }
        break;
    }
    }
    refreshCanCapture(this, d->canCapture, canCapture());
}

void CaptureController::pointerReleased(qreal x, qreal y, int modifiers)
{
    if (d->finished)
        return;
    const QPoint p = floored(x, y);
    switch (d->mode) {
    case Region:
        changeSelection(this, d->selection, [&] { d->selection.release(p); });
        break;
    case Screen: {
        pointerMoved(x, y, modifiers);
        const bool click = d->pressed && !d->pressOutput.isEmpty() && d->pressOutput == d->hoverOutput;
        d->pressed = false;
        d->pressOutput.clear();
        if (click)
            capture();
        break;
    }
    case Window: {
        pointerMoved(x, y, modifiers);
        const bool click = d->pressed && d->pressWindow >= 0 && d->pressWindow == d->hoverWindow;
        d->pressed = false;
        d->pressWindow = -1;
        if (click)
            capture();
        break;
    }
    }
    if (!d->finished)
        refreshCanCapture(this, d->canCapture, canCapture());
}

bool CaptureController::keyPressed(int key, int modifiers)
{
    switch (key) {
    case Qt::Key_Escape:
        cancel();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (canCapture())
            capture();
        return true;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
        break;
    default:
        return false;
    }
    if (d->finished || d->mode != Region || !hasSelection() || d->selection.dragging())
        return false;

    const bool shift = (modifiers & Qt::ShiftModifier) != 0;
    const int dx = key == Qt::Key_Left ? -1 : key == Qt::Key_Right ? 1 : 0;
    const int dy = key == Qt::Key_Up ? -1 : key == Qt::Key_Down ? 1 : 0;
    changeSelection(this, d->selection, [&] {
        if (shift)
            d->selection.resizeBy(dx, dy);
        else
            d->selection.nudge(dx, dy);
    });
    return true;
}

int CaptureController::handleAt(qreal x, qreal y) const
{
    // Only Region mode has handles; in the other modes the region is not being edited.
    if (d->mode != Region)
        return int(Handle::None);
    return int(d->selection.handleAt(floored(x, y)));
}

void CaptureController::capture()
{
    if (d->finished)
        return;

    RegionPlan plan;
    switch (d->mode) {
    case Region:
        if (!hasSelection())
            return;
        plan = planRegion(d->outputs, d->selection.rect());
        break;
    case Screen: {
        const QString name = d->hoverOutput.isEmpty() ? d->barOutput : d->hoverOutput;
        const int index = findOutput(d->outputs, name);
        if (index < 0)
            return;
        plan = planOutput(d->outputs[index]);
        break;
    }
    case Window:
        if (d->hoverWindow < 0 || d->hoverWindow >= d->clients.size())
            return;
        plan = planWindow(d->outputs, d->clients[d->hoverWindow]);
        break;
    }
    if (plan.isEmpty())
        return;

    const bool withPointer = d->pointer && !d->pointerImages.isEmpty();
    d->result = compose(plan, withPointer ? d->pointerImages : d->images);
    d->resultPlan = plan;

    Result outcome = Captured;
    if (d->config.copyOnCapture) {
        QString why;
        if (!copyImage(d->result, d->copyCommand, &why)) {
            d->error = why.isEmpty() ? QStringLiteral("copying to the clipboard failed") : why;
            outcome = Failed;
        }
    }
    if (outcome == Captured && d->config.saveOnCapture) {
        QString why;
        const QDateTime when = d->now.isValid() ? d->now : QDateTime::currentDateTime();
        d->savedPath = saveImage(d->result, d->config.resolvedSaveFolder(d->home), d->config.fileNamePattern, when,
                                 "png", &why);
        if (d->savedPath.isEmpty()) {
            d->error = why.isEmpty() ? QStringLiteral("saving the shot failed") : why;
            outcome = Failed;
        }
    }

    d->finished = true;
    emit finished(int(outcome));
}

void CaptureController::cancel()
{
    if (d->finished)
        return;
    d->finished = true;
    emit finished(int(Cancelled));
}

void CaptureController::expire()
{
    if (d->finished)
        return;
    d->finished = true;
    emit finished(int(TimedOut));
}

// --- results -----------------------------------------------------------------------------------------------------------

bool CaptureController::isFinished() const
{
    return d->finished;
}

QImage CaptureController::result() const
{
    return d->result;
}

RegionPlan CaptureController::resultPlan() const
{
    return d->resultPlan;
}

QString CaptureController::savedPath() const
{
    return d->savedPath;
}

QString CaptureController::error() const
{
    return d->error;
}

} // namespace rileyshot
