// SPDX-License-Identifier: GPL-3.0-or-later
#include "document.h"

#include "deliver.h"
#include "render.h"
#include "theme.h"

#include <QHash>
#include <QImageWriter>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <vector>

namespace rileyshot {

namespace {

// What one undo step restores.
struct State
{
    QList<Annotation> annotations;
    QRect crop;
};

bool samePoints(const QList<QPointF> &a, const QList<QPointF> &b)
{
    return a == b;
}

bool sameAnnotation(const Annotation &a, const Annotation &b)
{
    return a.id == b.id && a.kind == b.kind && a.colour == b.colour && a.width == b.width && a.filled == b.filled
        && samePoints(a.points, b.points) && a.rect == b.rect && a.position == b.position && a.text == b.text
        && a.textSize == b.textSize && a.number == b.number;
}

bool sameList(const QList<Annotation> &a, const QList<Annotation> &b)
{
    if (a.size() != b.size())
        return false;
    for (qsizetype i = 0; i < a.size(); ++i)
        if (!sameAnnotation(a.at(i), b.at(i)))
            return false;
    return true;
}

bool resizable(Annotation::Kind kind)
{
    return kind != Annotation::Text && kind != Annotation::Marker;
}

bool isStroke(Annotation::Kind kind)
{
    return kind == Annotation::Pen || kind == Annotation::Highlighter || kind == Annotation::Line
        || kind == Annotation::Arrow;
}

void translate(Annotation &a, const QPointF &delta)
{
    for (QPointF &p : a.points)
        p += delta;
    a.rect.translate(delta);
    a.position += delta;
}

// Maps a's geometry from oldBounds to newBounds (document.h setAnnotationRect).
void mapToBounds(Annotation &a, const QRectF &oldBounds, const QRectF &newBounds)
{
    if (isStroke(a.kind)) {
        for (QPointF &p : a.points) {
            const qreal x = oldBounds.width() != 0
                ? newBounds.x() + (p.x() - oldBounds.x()) * newBounds.width() / oldBounds.width()
                : p.x() + newBounds.x() - oldBounds.x();
            const qreal y = oldBounds.height() != 0
                ? newBounds.y() + (p.y() - oldBounds.y()) * newBounds.height() / oldBounds.height()
                : p.y() + newBounds.y() - oldBounds.y();
            p = QPointF(x, y);
        }
    } else {
        a.rect = newBounds.normalized();
    }
}

// The 8 handles of bounds: corners and edge midpoints, clockwise from the top-left.
QList<QPointF> handlesOf(const QRectF &b)
{
    const qreal cx = b.center().x(), cy = b.center().y();
    return {b.topLeft(), QPointF(cx, b.top()), b.topRight(), QPointF(b.right(), cy),
            b.bottomRight(), QPointF(cx, b.bottom()), b.bottomLeft(), QPointF(b.left(), cy)};
}

// Bounds with handle h dragged to point; the opposite side stays.
QRectF dragHandle(const QRectF &b, int h, const QPointF &point)
{
    qreal left = b.left(), top = b.top(), right = b.right(), bottom = b.bottom();
    const bool moveLeft = h == 0 || h == 6 || h == 7;
    const bool moveRight = h == 2 || h == 3 || h == 4;
    const bool moveTop = h == 0 || h == 1 || h == 2;
    const bool moveBottom = h == 4 || h == 5 || h == 6;
    if (moveLeft)
        left = point.x();
    if (moveRight)
        right = point.x();
    if (moveTop)
        top = point.y();
    if (moveBottom)
        bottom = point.y();
    return QRectF(QPointF(left, top), QPointF(right, bottom)).normalized();
}

} // namespace

struct Document::Private
{
    enum Drag { None, Create, Move, Resize, Erase, CropDrag };

    QImage base;
    State cur;
    std::vector<State> history; // history[index] is the state before any drag under way
    size_t index = 0;
    int nextId = 1;

    int tool = Document::Pen;
    QHash<int, QColor> colours;
    qreal strokeWidth = 4;
    bool filled = false;
    qreal textSize = 24;
    QString fontFamily = QStringLiteral("IBM Plex Mono");
    QColor markerText = QColor(0xff, 0xff, 0xff);
    int selected = -1;

    Drag drag = None;
    QPointF pressPoint, lastPoint;
    Annotation pending; // Create
    bool hasPending = false;
    Annotation dragOrig; // Move, Resize
    int handle = -1;

    mutable QImage committed; // base plus cur.annotations, full size
    mutable bool committedDirty = true;
    mutable QImage output;
    mutable bool outputDirty = true;

    QStringList copyCommand = copyCommandFromEnvironment();
    QString saveFolder;
    QString fileNamePattern = QStringLiteral("Screenshot_%Y-%m-%d_%H-%M-%S");
    QDateTime now;
    QString lastError;
    QString lastSavedPath;

    render::Style style() const { return {fontFamily, markerText}; }

    qsizetype indexOf(int id) const
    {
        for (qsizetype i = 0; i < cur.annotations.size(); ++i)
            if (cur.annotations.at(i).id == id)
                return i;
        return -1;
    }

    QColor colourFor(int t) const
    {
        const auto it = colours.constFind(t);
        if (it != colours.constEnd())
            return it.value();
        const Theme theme;
        if (t == Document::Highlighter)
            return theme.yellow;
        if (t == Document::Marker)
            return theme.accent;
        return theme.red;
    }

    void invalidate(bool annotationsChanged = true)
    {
        if (annotationsChanged)
            committedDirty = true;
        outputDirty = true;
    }

    // Gives a its id, appends it and makes that one undo step.
    int add(Document *q, Annotation a)
    {
        a.id = nextId++;
        cur.annotations.append(a);
        step(q);
        return a.id;
    }

    // Records cur as one undo step (dropping anything that could be redone) and emits what changed.
    void step(Document *q)
    {
        const QRect cropBefore = history[index].crop;
        history.resize(index + 1);
        history.push_back(cur);
        index = history.size() - 1;
        stateChanged(q, cropBefore);
    }

    // After cur changed (a step, an undo, a redo or a live drag): fix the selection, drop the caches, emit.
    void stateChanged(Document *q, const QRect &cropBefore, bool undoState = true)
    {
        invalidate();
        if (selected != -1 && indexOf(selected) < 0)
            selected = -1;
        if (cropBefore != cur.crop)
            emit q->cropChanged();
        emit q->changed();
        emit q->selectionChanged();
        if (undoState)
            emit q->undoStateChanged();
    }
};

Document::Document(QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->history.push_back(d->cur);
}

Document::~Document()
{
    delete d;
}

// --- the shot -----------------------------------------------------------------------------------------------------

void Document::setImage(const QImage &image)
{
    d->base = image.convertToFormat(QImage::Format_RGB32);
    d->cur = State{{}, d->base.rect()};
    d->history.clear();
    d->history.push_back(d->cur);
    d->index = 0;
    d->nextId = 1;
    d->drag = Private::None;
    d->hasPending = false;
    const bool hadSelection = d->selected != -1;
    d->selected = -1;
    d->invalidate();
    emit imageChanged();
    emit cropChanged();
    emit changed();
    emit undoStateChanged();
    if (hadSelection)
        emit selectionChanged();
}

QImage Document::image() const { return d->base; }
QSize Document::imageSize() const { return d->base.size(); }
QRect Document::cropRect() const { return d->cur.crop; }
QSize Document::outputSize() const { return d->cur.crop.size(); }

QImage Document::render() const
{
    if (d->committedDirty) {
        d->committed = d->base;
        if (!d->cur.annotations.isEmpty())
            render::paint(d->committed, d->cur.annotations, d->style());
        d->committedDirty = false;
        d->outputDirty = true;
    }
    if (d->outputDirty) {
        QImage full = d->committed;
        if (d->hasPending)
            render::paint(full, {d->pending}, d->style());
        d->output = d->cur.crop == full.rect() ? full : full.copy(d->cur.crop);
        d->outputDirty = false;
    }
    return d->output;
}

// --- tool settings ------------------------------------------------------------------------------------------------

int Document::tool() const { return d->tool; }

void Document::setTool(int tool)
{
    if (tool < Select || tool > Pixelate || tool == d->tool)
        return;
    if (d->drag != Private::None)
        release(d->lastPoint);
    d->tool = tool;
    if (tool != Select)
        setSelectedId(-1);
    emit toolChanged();
    emit colourChanged();
}

QColor Document::colour() const { return d->colourFor(d->tool); }

void Document::setColour(const QColor &colour)
{
    setToolColour(d->tool, colour);
}

QColor Document::toolColour(int tool) const { return d->colourFor(tool); }

void Document::setToolColour(int tool, const QColor &colour)
{
    if (tool < Select || tool > Pixelate || !colour.isValid())
        return;
    if (d->colourFor(tool) == colour && d->colours.contains(tool))
        return;
    d->colours.insert(tool, colour);
    if (tool == d->tool)
        emit colourChanged();
}

qreal Document::strokeWidth() const { return d->strokeWidth; }

void Document::setStrokeWidth(qreal width)
{
    width = std::max(width, qreal(1));
    if (width == d->strokeWidth)
        return;
    d->strokeWidth = width;
    emit strokeWidthChanged();
}

bool Document::filled() const { return d->filled; }

void Document::setFilled(bool on)
{
    if (on == d->filled)
        return;
    d->filled = on;
    emit filledChanged();
}

qreal Document::textSize() const { return d->textSize; }

void Document::setTextSize(qreal size)
{
    size = std::max(size, qreal(1));
    if (size == d->textSize)
        return;
    d->textSize = size;
    emit textSizeChanged();
}

QString Document::fontFamily() const { return d->fontFamily; }

void Document::setFontFamily(const QString &family)
{
    if (family.isEmpty() || family == d->fontFamily)
        return;
    d->fontFamily = family;
    d->invalidate();
    emit fontFamilyChanged();
    emit changed();
}

QColor Document::markerTextColour() const { return d->markerText; }

void Document::setMarkerTextColour(const QColor &colour)
{
    if (!colour.isValid() || colour == d->markerText)
        return;
    d->markerText = colour;
    d->invalidate();
    emit changed();
}

// --- adding objects -----------------------------------------------------------------------------------------------

int Document::addStroke(Annotation::Kind kind, const QList<QPointF> &points, const QColor &colour, qreal width)
{
    if ((kind != Annotation::Pen && kind != Annotation::Highlighter) || points.isEmpty())
        return -1;
    Annotation a;
    a.kind = kind;
    a.colour = colour;
    a.width = std::max(width, qreal(1));
    a.points = points;
    return d->add(this, a);
}

int Document::addLine(const QPointF &from, const QPointF &to, const QColor &colour, qreal width)
{
    if (from == to)
        return -1;
    Annotation a;
    a.kind = Annotation::Line;
    a.colour = colour;
    a.width = std::max(width, qreal(1));
    a.points = {from, to};
    return d->add(this, a);
}

int Document::addArrow(const QPointF &from, const QPointF &to, const QColor &colour, qreal width)
{
    if (from == to)
        return -1;
    Annotation a;
    a.kind = Annotation::Arrow;
    a.colour = colour;
    a.width = std::max(width, qreal(1));
    a.points = {from, to};
    return d->add(this, a);
}

int Document::addRectangle(const QRectF &rect, const QColor &colour, qreal width, bool filled)
{
    const QRectF r = rect.normalized();
    if (r.width() <= 0 || r.height() <= 0)
        return -1;
    Annotation a;
    a.kind = Annotation::Rectangle;
    a.colour = colour;
    a.width = std::max(width, qreal(1));
    a.filled = filled;
    a.rect = r;
    return d->add(this, a);
}

int Document::addEllipse(const QRectF &rect, const QColor &colour, qreal width, bool filled)
{
    const QRectF r = rect.normalized();
    if (r.width() <= 0 || r.height() <= 0)
        return -1;
    Annotation a;
    a.kind = Annotation::Ellipse;
    a.colour = colour;
    a.width = std::max(width, qreal(1));
    a.filled = filled;
    a.rect = r;
    return d->add(this, a);
}

int Document::addText(const QPointF &topLeft, const QString &text, const QColor &colour, qreal pixelSize)
{
    if (text.isEmpty())
        return -1;
    Annotation a;
    a.kind = Annotation::Text;
    a.colour = colour;
    a.position = topLeft;
    a.text = text;
    a.textSize = pixelSize;
    return d->add(this, a);
}

int Document::addMarker(const QPointF &centre, const QColor &colour)
{
    int highest = 0;
    for (const Annotation &a : d->cur.annotations)
        if (a.kind == Annotation::Marker)
            highest = std::max(highest, a.number);
    Annotation a;
    a.kind = Annotation::Marker;
    a.colour = colour;
    a.position = centre;
    a.number = highest + 1;
    return d->add(this, a);
}

int Document::addBlur(const QRectF &rect)
{
    const QRect r = render::snapRect(rect, d->base.size());
    if (r.isEmpty())
        return -1;
    Annotation a;
    a.kind = Annotation::Blur;
    a.rect = QRectF(r);
    return d->add(this, a);
}

int Document::addPixelate(const QRectF &rect)
{
    const QRect r = render::snapRect(rect, d->base.size());
    if (r.isEmpty())
        return -1;
    Annotation a;
    a.kind = Annotation::Pixelate;
    a.rect = QRectF(r);
    return d->add(this, a);
}

int Document::placeText(const QPointF &topLeft, const QString &text)
{
    return addText(topLeft, text, d->colourFor(Text), d->textSize);
}

// --- reading objects ----------------------------------------------------------------------------------------------

int Document::annotationCount() const { return int(d->cur.annotations.size()); }

QList<int> Document::ids() const
{
    QList<int> out;
    out.reserve(d->cur.annotations.size());
    for (const Annotation &a : d->cur.annotations)
        out.append(a.id);
    return out;
}

Annotation Document::annotation(int id) const
{
    const qsizetype i = d->indexOf(id);
    return i < 0 ? Annotation() : d->cur.annotations.at(i);
}

QRectF Document::annotationBounds(int id) const
{
    const qsizetype i = d->indexOf(id);
    return i < 0 ? QRectF() : render::bounds(d->cur.annotations.at(i), d->style());
}

int Document::annotationAt(const QPointF &point) const
{
    const render::Style style = d->style();
    for (qsizetype i = d->cur.annotations.size() - 1; i >= 0; --i)
        if (render::hits(d->cur.annotations.at(i), point, style))
            return d->cur.annotations.at(i).id;
    return -1;
}

// --- changing objects ---------------------------------------------------------------------------------------------

bool Document::moveAnnotation(int id, const QPointF &delta)
{
    const qsizetype i = d->indexOf(id);
    if (i < 0 || delta.isNull())
        return false;
    translate(d->cur.annotations[i], delta);
    d->step(this);
    return true;
}

bool Document::setAnnotationRect(int id, const QRectF &bounds)
{
    const qsizetype i = d->indexOf(id);
    if (i < 0)
        return false;
    Annotation &a = d->cur.annotations[i];
    if (!resizable(a.kind))
        return false;
    const Annotation before = a;
    mapToBounds(a, render::bounds(a, d->style()), bounds);
    if (sameAnnotation(a, before))
        return false;
    d->step(this);
    return true;
}

bool Document::removeAnnotation(int id)
{
    const qsizetype i = d->indexOf(id);
    if (i < 0)
        return false;
    d->cur.annotations.removeAt(i);
    d->step(this);
    return true;
}

bool Document::erase(const QPointF &point)
{
    const int id = annotationAt(point);
    return id >= 0 && removeAnnotation(id);
}

bool Document::crop(const QRect &rect)
{
    const QRect r = rect.normalized() & d->base.rect();
    if (r.isEmpty() || r == d->cur.crop)
        return false;
    d->cur.crop = r;
    d->step(this);
    return true;
}

// --- the tools ----------------------------------------------------------------------------------------------------

void Document::press(const QPointF &point, int modifiers)
{
    Q_UNUSED(modifiers);
    if (d->drag != Private::None)
        release(d->lastPoint);
    d->pressPoint = d->lastPoint = point;
    d->hasPending = false;
    switch (d->tool) {
    case Pen:
    case Highlighter:
        d->pending = Annotation();
        d->pending.kind = d->tool == Pen ? Annotation::Pen : Annotation::Highlighter;
        d->pending.colour = d->colourFor(d->tool);
        d->pending.width = d->strokeWidth;
        d->pending.points = {point};
        d->hasPending = true;
        d->drag = Private::Create;
        d->invalidate(false);
        emit changed();
        break;
    case Line:
    case Arrow:
    case Rectangle:
    case Ellipse:
    case Blur:
    case Pixelate:
        d->drag = Private::Create;
        break;
    case Crop:
        d->drag = Private::CropDrag;
        break;
    case Marker:
        addMarker(point, d->colourFor(Marker));
        break;
    case Text:
        emit textRequested(point);
        break;
    case Eraser:
        d->drag = Private::Erase;
        move(point);
        break;
    case Select: {
        const qsizetype si = d->indexOf(d->selected);
        if (si >= 0 && resizable(d->cur.annotations.at(si).kind)) {
            const QList<QPointF> handles = handlesOf(selectedBounds());
            qreal best = 6;
            int found = -1;
            for (int h = 0; h < handles.size(); ++h) {
                const qreal dist = std::hypot(handles.at(h).x() - point.x(), handles.at(h).y() - point.y());
                if (dist <= best) {
                    best = dist;
                    found = h;
                }
            }
            if (found >= 0) {
                d->drag = Private::Resize;
                d->handle = found;
                d->dragOrig = d->cur.annotations.at(si);
                break;
            }
        }
        const int id = annotationAt(point);
        setSelectedId(id);
        if (id >= 0) {
            d->drag = Private::Move;
            d->dragOrig = annotation(id);
        }
        break;
    }
    default:
        break;
    }
}

void Document::move(const QPointF &point, int modifiers)
{
    Q_UNUSED(modifiers);
    if (d->drag == Private::None)
        return;
    d->lastPoint = point;
    switch (d->drag) {
    case Private::Create: {
        if (d->tool == Pen || d->tool == Highlighter) {
            if (d->pending.points.isEmpty() || d->pending.points.last() != point)
                d->pending.points.append(point);
        } else {
            const QRectF r = QRectF(d->pressPoint, point).normalized();
            Annotation a;
            a.colour = d->colourFor(d->tool);
            a.width = d->strokeWidth;
            a.filled = d->filled;
            bool show = false;
            switch (d->tool) {
            case Line:
            case Arrow:
                a.kind = d->tool == Line ? Annotation::Line : Annotation::Arrow;
                a.points = {d->pressPoint, point};
                show = d->pressPoint != point;
                break;
            case Rectangle:
            case Ellipse:
                a.kind = d->tool == Rectangle ? Annotation::Rectangle : Annotation::Ellipse;
                a.rect = r;
                show = r.width() > 0 && r.height() > 0;
                break;
            case Blur:
            case Pixelate:
                a.kind = d->tool == Blur ? Annotation::Blur : Annotation::Pixelate;
                a.rect = QRectF(render::snapRect(r, d->base.size()));
                show = !a.rect.isEmpty();
                break;
            default:
                break;
            }
            d->pending = a;
            d->hasPending = show;
        }
        d->invalidate(false);
        emit changed();
        break;
    }
    case Private::Move:
    case Private::Resize: {
        const qsizetype i = d->indexOf(d->dragOrig.id);
        if (i < 0)
            break;
        Annotation a = d->dragOrig;
        if (d->drag == Private::Move) {
            translate(a, point - d->pressPoint);
        } else {
            const QRectF from = render::bounds(d->dragOrig, d->style());
            mapToBounds(a, from, dragHandle(from, d->handle, point));
        }
        d->cur.annotations[i] = a;
        d->stateChanged(this, d->cur.crop, false);
        break;
    }
    case Private::Erase: {
        const int id = annotationAt(point);
        const qsizetype i = d->indexOf(id);
        if (i >= 0) {
            d->cur.annotations.removeAt(i);
            d->stateChanged(this, d->cur.crop, false);
        }
        break;
    }
    case Private::CropDrag:
    case Private::None:
        break;
    }
}

void Document::release(const QPointF &point, int modifiers)
{
    if (d->drag == Private::None)
        return;
    move(point, modifiers);
    const Private::Drag drag = d->drag;
    d->drag = Private::None;
    switch (drag) {
    case Private::Create: {
        d->hasPending = false;
        d->invalidate(false);
        const QColor colour = d->colourFor(d->tool);
        const QRectF r = QRectF(d->pressPoint, point).normalized();
        int id = -1;
        switch (d->tool) {
        case Pen:
        case Highlighter:
            id = addStroke(d->pending.kind, d->pending.points, colour, d->strokeWidth);
            break;
        case Line:
            id = addLine(d->pressPoint, point, colour, d->strokeWidth);
            break;
        case Arrow:
            id = addArrow(d->pressPoint, point, colour, d->strokeWidth);
            break;
        case Rectangle:
            id = addRectangle(r, colour, d->strokeWidth, d->filled);
            break;
        case Ellipse:
            id = addEllipse(r, colour, d->strokeWidth, d->filled);
            break;
        case Blur:
            id = addBlur(r);
            break;
        case Pixelate:
            id = addPixelate(r);
            break;
        default:
            break;
        }
        if (id < 0)
            emit changed(); // the preview is gone
        break;
    }
    case Private::Move:
    case Private::Resize:
    case Private::Erase:
        if (!sameList(d->cur.annotations, d->history[d->index].annotations))
            d->step(this);
        break;
    case Private::CropDrag:
        crop(QRectF(d->pressPoint, point).normalized().toAlignedRect());
        break;
    case Private::None:
        break;
    }
}

int Document::selectedId() const { return d->selected; }

void Document::setSelectedId(int id)
{
    if (d->indexOf(id) < 0)
        id = -1;
    if (id == d->selected)
        return;
    d->selected = id;
    emit selectionChanged();
}

QRectF Document::selectedBounds() const { return annotationBounds(d->selected); }

bool Document::removeSelected()
{
    return d->selected >= 0 && removeAnnotation(d->selected);
}

// --- undo ---------------------------------------------------------------------------------------------------------

bool Document::canUndo() const { return d->index > 0; }
bool Document::canRedo() const { return d->index + 1 < d->history.size(); }

void Document::undo()
{
    if (!canUndo())
        return;
    const QRect cropBefore = d->cur.crop;
    d->drag = Private::None;
    d->hasPending = false;
    --d->index;
    d->cur = d->history[d->index];
    d->stateChanged(this, cropBefore);
}

void Document::redo()
{
    if (!canRedo())
        return;
    const QRect cropBefore = d->cur.crop;
    d->drag = Private::None;
    d->hasPending = false;
    ++d->index;
    d->cur = d->history[d->index];
    d->stateChanged(this, cropBefore);
}

// --- save and copy ------------------------------------------------------------------------------------------------

QStringList Document::copyCommand() const { return d->copyCommand; }
void Document::setCopyCommand(const QStringList &command) { d->copyCommand = command; }
QString Document::saveFolder() const { return d->saveFolder; }
void Document::setSaveFolder(const QString &folder) { d->saveFolder = folder; }
QString Document::fileNamePattern() const { return d->fileNamePattern; }
void Document::setFileNamePattern(const QString &pattern) { d->fileNamePattern = pattern; }
void Document::setNow(const QDateTime &now) { d->now = now; }

bool Document::copyToClipboard()
{
    QString error;
    const bool ok = copyImage(render(), d->copyCommand, &error);
    const QString message = ok ? QString() : (error.isEmpty() ? QStringLiteral("copy failed") : error);
    if (message != d->lastError) {
        d->lastError = message;
        emit lastErrorChanged();
    }
    return ok;
}

QString Document::quickSave(const QString &format)
{
    const QString f = format.toLower();
    const QByteArray fmt = (f == QLatin1String("jpg") || f == QLatin1String("jpeg")) ? QByteArray("jpg")
                                                                                       : QByteArray("png");
    QString error;
    const QDateTime when = d->now.isValid() ? d->now : QDateTime::currentDateTime();
    const QString path = saveImage(render(), d->saveFolder, d->fileNamePattern, when, fmt, &error);
    const QString message = path.isEmpty() ? (error.isEmpty() ? QStringLiteral("save failed") : error) : QString();
    if (message != d->lastError) {
        d->lastError = message;
        emit lastErrorChanged();
    }
    if (!path.isEmpty()) {
        d->lastSavedPath = path;
        emit saved(path);
    }
    return path;
}

bool Document::saveAs(const QString &pathOrUrl)
{
    QString path = pathOrUrl;
    if (path.startsWith(QLatin1String("file:"), Qt::CaseInsensitive))
        path = QUrl(pathOrUrl).toLocalFile();
    const QString lower = path.toLower();
    const bool jpeg = lower.endsWith(QLatin1String(".jpg")) || lower.endsWith(QLatin1String(".jpeg"));
    QString message;
    if (path.isEmpty()) {
        message = QStringLiteral("save as: no file name");
    } else {
        QImageWriter writer(path, jpeg ? QByteArray("JPEG") : QByteArray("PNG"));
        if (jpeg)
            writer.setQuality(92);
        if (!writer.write(render()))
            message = QStringLiteral("save as %1: %2").arg(path, writer.errorString());
    }
    if (message != d->lastError) {
        d->lastError = message;
        emit lastErrorChanged();
    }
    if (!message.isEmpty())
        return false;
    d->lastSavedPath = path;
    emit saved(path);
    return true;
}

QString Document::lastError() const { return d->lastError; }
QString Document::lastSavedPath() const { return d->lastSavedPath; }

} // namespace rileyshot
