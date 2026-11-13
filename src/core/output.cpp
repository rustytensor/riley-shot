// SPDX-License-Identifier: GPL-3.0-or-later
#include "output.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <cmath>

namespace rileyshot {

QSize Output::deviceSize() const
{
    return (transform % 2 != 0) ? modeSize.transposed() : modeSize;
}

QSize Output::logicalSize() const
{
    const QSize d = deviceSize();
    return QSize(int(std::lround(d.width() / scale)), int(std::lround(d.height() / scale)));
}

QRect Output::layoutRect() const
{
    return QRect(layoutPos, logicalSize());
}

QPointF Output::layoutToDevice(QPointF layout) const
{
    return (layout - QPointF(layoutPos)) * scale;
}

QPointF Output::deviceToLayout(QPointF device) const
{
    return device / scale + QPointF(layoutPos);
}

QRect Output::layoutToDeviceRect(const QRectF &layout) const
{
    const QPointF tl = layoutToDevice(layout.topLeft());
    const QPointF br = layoutToDevice(QPointF(layout.x() + layout.width(), layout.y() + layout.height()));
    const QSize d = deviceSize();
    const double left = std::max(0.0, std::floor(tl.x() + kEpsilon));
    const double top = std::max(0.0, std::floor(tl.y() + kEpsilon));
    const double right = std::min(double(d.width()), std::ceil(br.x() - kEpsilon));
    const double bottom = std::min(double(d.height()), std::ceil(br.y() - kEpsilon));
    if (right <= left || bottom <= top)
        return QRect();
    return QRect(int(left), int(top), int(right - left), int(bottom - top));
}

namespace {

QList<Output> fail(QString *error, const QString &reason)
{
    if (error)
        *error = reason;
    return {};
}

} // namespace

QList<Output> parseMonitors(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(error, QStringLiteral("hyprctl monitors: bad JSON: %1").arg(parseError.errorString()));
    if (!doc.isArray())
        return fail(error, QStringLiteral("hyprctl monitors: expected a JSON array"));

    static const char *const required[] = {"name", "width", "height", "x", "y", "scale", "transform"};
    QList<Output> outputs;
    const QJsonArray array = doc.array();
    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!array[i].isObject())
            return fail(error, QStringLiteral("hyprctl monitors: entry %1 is not an object").arg(i));
        const QJsonObject o = array[i].toObject();
        for (const char *key : required) {
            const QJsonValue v = o.value(QLatin1String(key));
            const bool ok = (QLatin1String(key) == QLatin1String("name")) ? v.isString() : v.isDouble();
            if (!ok)
                return fail(error, QStringLiteral("hyprctl monitors: entry %1 has no valid \"%2\"")
                                       .arg(i)
                                       .arg(QLatin1String(key)));
        }
        if (o.value(QLatin1String("disabled")).toBool(false))
            continue;
        Output out;
        out.name = o.value(QLatin1String("name")).toString();
        out.layoutPos = QPoint(o.value(QLatin1String("x")).toInt(), o.value(QLatin1String("y")).toInt());
        out.modeSize = QSize(o.value(QLatin1String("width")).toInt(), o.value(QLatin1String("height")).toInt());
        out.scale = o.value(QLatin1String("scale")).toDouble();
        out.transform = o.value(QLatin1String("transform")).toInt();
        out.activeWorkspace = o.value(QLatin1String("activeWorkspace")).toObject().value(QLatin1String("id")).toInt();
        if (out.scale <= 0)
            return fail(error, QStringLiteral("hyprctl monitors: %1 has scale %2").arg(out.name).arg(out.scale));
        outputs.append(out);
    }
    if (error)
        error->clear();
    return outputs;
}

const Output *outputAt(const QList<Output> &outputs, QPointF layout)
{
    for (const Output &o : outputs) {
        const QRect r = o.layoutRect();
        if (layout.x() >= r.x() && layout.x() < r.x() + r.width() && layout.y() >= r.y()
            && layout.y() < r.y() + r.height())
            return &o;
    }
    return nullptr;
}

int findOutput(const QList<Output> &outputs, const QString &name)
{
    for (qsizetype i = 0; i < outputs.size(); ++i)
        if (outputs[i].name == name)
            return int(i);
    return -1;
}

} // namespace rileyshot
