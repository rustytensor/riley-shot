// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli.h"

#include <algorithm>

namespace rileyshot {

CliOptions parseCli(const QStringList &arguments)
{
    using Command = CliOptions::Command;
    CliOptions o;

    for (const QString &a : arguments) {
        if (a == QLatin1String("--help") || a == QLatin1String("-h")) {
            o.command = Command::Help;
            return o;
        }
    }

    auto error = [](const QString &why) {
        CliOptions e;
        e.command = Command::Error;
        e.error = why;
        return e;
    };

    qsizetype i = 0;
    if (!arguments.isEmpty() && arguments.first() == QLatin1String("grab")) {
        o.command = Command::Grab;
        i = 1;
    } else if (!arguments.isEmpty() && arguments.first() == QLatin1String("edit")) {
        o.command = Command::Edit;
        i = 1;
    } else if (!arguments.isEmpty() && arguments.first() == QLatin1String("capture")) {
        i = 1;
    }

    if (o.command == Command::Grab) {
        bool haveOutput = false, haveOut = false;
        for (; i < arguments.size(); ++i) {
            const QString &a = arguments[i];
            if (a == QLatin1String("--output") || a == QLatin1String("--out")) {
                const bool isOutput = a == QLatin1String("--output");
                if (isOutput ? haveOutput : haveOut)
                    return error(QStringLiteral("%1 given twice").arg(a));
                if (i + 1 >= arguments.size() || arguments[i + 1].startsWith(QLatin1Char('-')))
                    return error(QStringLiteral("%1 needs a value").arg(a));
                const QString value = arguments[++i];
                if (value.isEmpty())
                    return error(QStringLiteral("%1 needs a value").arg(a));
                if (isOutput) {
                    o.output = value;
                    haveOutput = true;
                } else {
                    o.outFile = value;
                    haveOut = true;
                }
            } else if (a == QLatin1String("--pointer")) {
                if (o.pointer)
                    return error(QStringLiteral("--pointer given twice"));
                o.pointer = true;
            } else {
                return error(QStringLiteral("grab does not take \"%1\"").arg(a));
            }
        }
        if (!haveOutput)
            return error(QStringLiteral("grab needs --output NAME"));
        if (!haveOut)
            return error(QStringLiteral("grab needs --out FILE"));
        return o;
    }

    // The test aids (M2), shared by capture and edit: each once, each with a value. Returns false (and sets *why)
    // on a bad one; *i is moved past the value.
    bool haveOutputs = false, haveTimer = false, haveReport = false;
    auto testAid = [&](qsizetype *i, bool allowOutputs, QString *why) -> bool {
        const QString &a = arguments[*i];
        if (a == QLatin1String("--outputs") && !allowOutputs) {
            *why = QStringLiteral("edit does not take --outputs");
            return false;
        }
        bool &seen = a == QLatin1String("--outputs") ? haveOutputs
                     : a == QLatin1String("--report") ? haveReport
                                                      : haveTimer;
        if (seen) {
            *why = QStringLiteral("%1 given twice").arg(a);
            return false;
        }
        seen = true;
        // A following option ("--report --outputs ...") is a missing value, not a value. A single "-" start
        // is kept as a value so "--exit-after-ms -5" is reported as a bad number.
        if (*i + 1 >= arguments.size() || arguments[*i + 1].startsWith(QLatin1String("--"))) {
            *why = QStringLiteral("%1 needs a value").arg(a);
            return false;
        }
        const QString value = arguments[++*i];
        if (value.isEmpty()) {
            *why = QStringLiteral("%1 needs a value").arg(a);
            return false;
        }
        if (a == QLatin1String("--outputs")) {
            const QStringList names = value.split(QLatin1Char(','));
            for (const QString &n : names)
                if (n.isEmpty()) {
                    *why = QStringLiteral("--outputs has an empty name in \"%1\"").arg(value);
                    return false;
                }
            o.outputs = names;
        } else if (a == QLatin1String("--exit-after-ms")) {
            // Decimal digits only; leading zeros dropped; any number of digits (a huge one is just above 10000).
            bool digits = true;
            for (const QChar ch : value)
                digits = digits && ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
            qsizetype first = 0;
            while (digits && first < value.size() && value[first] == QLatin1Char('0'))
                ++first;
            const QString significant = value.mid(first);
            if (!digits || significant.isEmpty()) {
                *why = QStringLiteral("--exit-after-ms needs a whole number of milliseconds >= 1, not \"%1\"")
                           .arg(value);
                return false;
            }
            o.exitAfterMs = significant.size() > 5 ? 10000 : std::min(significant.toInt(), 10000);
        } else {
            o.reportFile = value;
        }
        return true;
    };
    auto isTestAid = [](const QString &a) {
        return a == QLatin1String("--outputs") || a == QLatin1String("--exit-after-ms")
               || a == QLatin1String("--report");
    };

    if (o.command == Command::Edit) {
        bool haveFile = false;
        for (; i < arguments.size(); ++i) {
            const QString &a = arguments[i];
            if (isTestAid(a)) {
                QString why;
                if (!testAid(&i, false, &why))
                    return error(why);
            } else if (a.isEmpty()) {
                return error(QStringLiteral("edit needs a file name, not an empty word"));
            } else if (a.startsWith(QLatin1Char('-'))) {
                return error(QStringLiteral("edit does not take \"%1\"").arg(a));
            } else if (haveFile) {
                return error(QStringLiteral("edit takes one file, not \"%1\" as well").arg(a));
            } else {
                o.inFile = a;
                haveFile = true;
            }
        }
        if (!haveFile)
            return error(QStringLiteral("edit needs FILE"));
        return o;
    }

    bool haveMode = false, haveNoEditor = false;
    for (; i < arguments.size(); ++i) {
        const QString &a = arguments[i];

        if (isTestAid(a)) {
            QString why;
            if (!testAid(&i, true, &why))
                return error(why);
            continue;
        }

        Mode mode = Mode::Bar;
        if (a == QLatin1String("--region"))
            mode = Mode::Region;
        else if (a == QLatin1String("--screen"))
            mode = Mode::Screen;
        else if (a == QLatin1String("--window"))
            mode = Mode::Window;

        if (mode != Mode::Bar) {
            if (haveMode)
                return error(QStringLiteral("only one of --region, --screen and --window"));
            o.mode = mode;
            haveMode = true;
        } else if (a == QLatin1String("--no-editor")) {
            if (haveNoEditor)
                return error(QStringLiteral("--no-editor given twice"));
            o.editor = false;
            haveNoEditor = true;
        } else if (a.startsWith(QLatin1Char('-'))) {
            return error(QStringLiteral("unknown option \"%1\"").arg(a));
        } else {
            return error(QStringLiteral("unexpected word \"%1\"").arg(a));
        }
    }
    return o;
}

} // namespace rileyshot
