// SPDX-License-Identifier: GPL-3.0-or-later
#include "deliver.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

namespace rileyshot {

QStringList defaultCopyCommand()
{
    return {QStringLiteral("wl-copy"), QStringLiteral("--type"), QStringLiteral("image/png")};
}

QStringList copyCommandFromEnvironment()
{
    const QString value = qEnvironmentVariable("RILEYSHOT_COPY_COMMAND");
    if (value.trimmed().isEmpty())
        return defaultCopyCommand();
    const QStringList command = QProcess::splitCommand(value);
    return command.isEmpty() ? defaultCopyCommand() : command;
}

bool copyImage(const QImage &image, const QStringList &command, QString *error)
{
    auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (command.isEmpty())
        return fail(QStringLiteral("copy: no copy command"));
    if (image.isNull())
        return fail(QStringLiteral("copy: no image"));

    QByteArray png;
    {
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG"))
            return fail(QStringLiteral("copy: cannot encode the image as PNG"));
    }

    QProcess p;
    p.setProcessChannelMode(QProcess::ForwardedOutputChannel);
    p.start(command.first(), command.mid(1)); // a program and its arguments: no shell
    if (!p.waitForStarted(5000))
        return fail(QStringLiteral("copy: cannot start %1: %2").arg(command.first(), p.errorString()));
    p.write(png);
    p.closeWriteChannel();
    if (!p.waitForFinished(5000)) {
        p.kill();
        p.waitForFinished(1000);
        return fail(QStringLiteral("copy: %1 took more than 5 s").arg(command.first()));
    }
    if (p.exitStatus() != QProcess::NormalExit)
        return fail(QStringLiteral("copy: %1 crashed").arg(command.first()));
    if (p.exitCode() != 0) {
        QString why = QStringLiteral("copy: %1 exited %2").arg(command.first()).arg(p.exitCode());
        const QString detail = QString::fromUtf8(p.readAllStandardError()).trimmed().section(QLatin1Char('\n'), 0, 0);
        if (!detail.isEmpty())
            why += QStringLiteral(" (%1)").arg(detail);
        return fail(why);
    }
    if (error)
        error->clear();
    return true;
}

QString fileNameFor(const QString &pattern, const QDateTime &time, const QString &extension)
{
    const QDate date = time.date();
    const QTime clock = time.time();
    auto two = [](int v) { return QStringLiteral("%1").arg(v, 2, 10, QLatin1Char('0')); };

    QString name;
    for (qsizetype i = 0; i < pattern.size(); ++i) {
        const QChar c = pattern[i];
        if (c != QLatin1Char('%') || i + 1 >= pattern.size()) {
            name += c;
            continue;
        }
        const QChar t = pattern[i + 1];
        switch (t.unicode()) {
        case 'Y': name += QStringLiteral("%1").arg(date.year(), 4, 10, QLatin1Char('0')); break;
        case 'm': name += two(date.month()); break;
        case 'd': name += two(date.day()); break;
        case 'H': name += two(clock.hour()); break;
        case 'M': name += two(clock.minute()); break;
        case 'S': name += two(clock.second()); break;
        case '%': name += QLatin1Char('%'); break;
        default: // not a token: kept as written
            name += c;
            name += t;
            break;
        }
        ++i;
    }
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    return name + QLatin1Char('.') + extension;
}

QString saveImage(const QImage &image, const QString &folder, const QString &pattern, const QDateTime &time,
                  const QByteArray &format, QString *error)
{
    auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return QString();
    };
    const QByteArray f = format.toLower();
    QString extension;
    const char *writer = nullptr;
    if (f == "png") {
        extension = QStringLiteral("png");
        writer = "PNG";
    } else if (f == "jpg" || f == "jpeg") {
        extension = QStringLiteral("jpg");
        writer = "JPEG";
    } else {
        return fail(QStringLiteral("save: unknown format \"%1\" (png, jpg or jpeg)").arg(QString::fromLatin1(format)));
    }
    if (image.isNull())
        return fail(QStringLiteral("save: no image"));
    if (!QDir().mkpath(folder))
        return fail(QStringLiteral("save: cannot create the folder %1").arg(folder));

    const QString name = fileNameFor(pattern, time, extension);
    const QString stem = name.left(name.size() - extension.size() - 1);
    const QDir dir(folder);
    for (int n = 1; n < 10000; ++n) {
        const QString candidate =
            dir.filePath(n == 1 ? name : QStringLiteral("%1-%2.%3").arg(stem).arg(n).arg(extension));
        if (QFileInfo::exists(candidate))
            continue;
        QFile file(candidate);
        // NewOnly: fails when the file appeared since the check, so nothing is ever overwritten.
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            if (QFileInfo::exists(candidate))
                continue;
            return fail(QStringLiteral("save: cannot write %1: %2").arg(candidate, file.errorString()));
        }
        if (!image.save(&file, writer) || !file.flush()) {
            file.close();
            file.remove();
            return fail(QStringLiteral("save: cannot write %1 as %2").arg(candidate, QString::fromLatin1(writer)));
        }
        file.close();
        if (error)
            error->clear();
        return candidate;
    }
    return fail(QStringLiteral("save: every name for %1 in %2 is taken").arg(name, folder));
}

} // namespace rileyshot
