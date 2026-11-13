// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme.h"

#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace rileyshot {

namespace {

// The member for each key, in keys() order.
QColor Theme::*member(const QString &key)
{
    static const QHash<QString, QColor Theme::*> table{
        {QStringLiteral("base"), &Theme::base},         {QStringLiteral("surface0"), &Theme::surface0},
        {QStringLiteral("surface1"), &Theme::surface1}, {QStringLiteral("text"), &Theme::text},
        {QStringLiteral("subtext"), &Theme::subtext},   {QStringLiteral("accent"), &Theme::accent},
        {QStringLiteral("mauve"), &Theme::mauve},       {QStringLiteral("lavender"), &Theme::lavender},
        {QStringLiteral("teal"), &Theme::teal},         {QStringLiteral("peach"), &Theme::peach},
        {QStringLiteral("green"), &Theme::green},       {QStringLiteral("red"), &Theme::red},
        {QStringLiteral("yellow"), &Theme::yellow},
    };
    return table.value(key, nullptr);
}

} // namespace

QStringList Theme::keys()
{
    return {QStringLiteral("base"),  QStringLiteral("surface0"), QStringLiteral("surface1"), QStringLiteral("text"),
            QStringLiteral("subtext"), QStringLiteral("accent"), QStringLiteral("mauve"),  QStringLiteral("lavender"),
            QStringLiteral("teal"),  QStringLiteral("peach"),    QStringLiteral("green"),    QStringLiteral("red"),
            QStringLiteral("yellow")};
}

QString Theme::defaultPath()
{
    return qEnvironmentVariable("HOME") + QStringLiteral("/.config/riley/bar.json");
}

Theme Theme::load(const QString &path)
{
    Theme theme;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return theme;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return theme;
    const QJsonObject object = doc.object();
    for (const QString &key : keys()) {
        const QJsonValue value = object.value(key);
        if (!value.isString())
            continue;
        const QColor colour = QColor::fromString(value.toString());
        if (colour.isValid())
            theme.*member(key) = colour;
    }
    const QJsonValue font = object.value(QStringLiteral("font"));
    if (font.isString() && !font.toString().isEmpty())
        theme.font = font.toString();
    return theme;
}

QColor Theme::colour(const QString &key) const
{
    QColor Theme::*m = member(key);
    return m ? this->*m : QColor();
}

QColor Theme::resolve(const QString &spec) const
{
    static const QRegularExpression rgb(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    if (rgb.match(spec).hasMatch())
        return QColor::fromString(spec);
    return colour(spec);
}

QList<QColor> Theme::palette() const
{
    return {red, peach, yellow, green, teal, lavender, mauve, text};
}

} // namespace rileyshot
