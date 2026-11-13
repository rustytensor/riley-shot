// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace rileyshot {

namespace {

QList<Client> fail(QString *error, const QString &reason)
{
    if (error)
        *error = reason;
    return {};
}

} // namespace

QList<Client> parseClients(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(error, QStringLiteral("hyprctl clients: bad JSON: %1").arg(parseError.errorString()));
    if (!doc.isArray())
        return fail(error, QStringLiteral("hyprctl clients: expected a JSON array"));

    QList<Client> clients;
    const QJsonArray array = doc.array();
    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!array[i].isObject())
            return fail(error, QStringLiteral("hyprctl clients: entry %1 is not an object").arg(i));
        const QJsonObject o = array[i].toObject();
        const QJsonArray at = o.value(QLatin1String("at")).toArray();
        const QJsonArray size = o.value(QLatin1String("size")).toArray();
        if (!o.value(QLatin1String("address")).isString() || at.size() != 2 || size.size() != 2)
            return fail(error, QStringLiteral("hyprctl clients: entry %1 has no valid address, at or size").arg(i));
        if (!o.value(QLatin1String("mapped")).toBool(true) || o.value(QLatin1String("hidden")).toBool(false))
            continue;

        Client c;
        c.address = o.value(QLatin1String("address")).toString();
        c.cls = o.value(QLatin1String("class")).toString();
        c.title = o.value(QLatin1String("title")).toString();
        c.rect = QRect(at[0].toInt(), at[1].toInt(), size[0].toInt(), size[1].toInt());
        c.monitor = o.value(QLatin1String("monitor")).toInt(-1);
        c.workspace = o.value(QLatin1String("workspace")).toObject().value(QLatin1String("id")).toInt();
        c.floating = o.value(QLatin1String("floating")).toBool(false);
        const QJsonValue fs = o.value(QLatin1String("fullscreen"));
        c.fullscreen = fs.isBool() ? fs.toBool() : fs.toInt() != 0;
        c.focusHistoryId = o.value(QLatin1String("focusHistoryID")).toInt(-1);
        clients.append(c);
    }
    if (error)
        error->clear();
    return clients;
}

RegionPlan planWindow(const QList<Output> &outputs, const Client &client)
{
    return planRegion(outputs, client.rect);
}

} // namespace rileyshot
