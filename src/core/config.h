// SPDX-License-Identifier: GPL-3.0-or-later
// riley-shot's configuration: $XDG_CONFIG_HOME/riley-shot/config.xml, validated against data/config.xsd (carried in
// the binary as the Qt resource ":/riley-shot/config.xsd"). Every element is optional; a missing one takes its
// default. A file that does not validate is not used at all: load() gives the defaults and an error.
//
// Owner: builder B (config.cpp). data/config.xsd is the gate writer's (it is the file format contract).
// The declarations below are the contract the M1 gate tests; do not change them.
#pragma once

#include <QByteArray>
#include <QString>

namespace rileyshot {

// The editor's starting colour per tool. Each is a theme key ("red", "accent", ... see Theme::colour) or "#rrggbb".
struct ToolColours
{
    QString pen = QStringLiteral("red");
    QString highlighter = QStringLiteral("yellow");
    QString shape = QStringLiteral("red");   // line, arrow, rectangle, ellipse
    QString text = QStringLiteral("red");
    QString marker = QStringLiteral("accent"); // numbered markers
    bool operator==(const ToolColours &) const = default;
};

struct Config
{
    QString saveFolder = QStringLiteral("~/Pictures/Screenshots"); // as written; see resolvedSaveFolder()
    QString fileNamePattern = QStringLiteral("Screenshot_%Y-%m-%d_%H-%M-%S"); // see fileNameFor() in deliver.h
    bool copyOnCapture = true;
    bool saveOnCapture = true;
    bool pointer = false; // the capture bar's "include pointer" toggle starts here
    ToolColours colours;
    bool operator==(const Config &) const = default;

    // saveFolder with a leading "~/" (or a bare "~") replaced by home.
    QString resolvedSaveFolder(const QString &home) const;

    // $XDG_CONFIG_HOME/riley-shot/config.xml; XDG_CONFIG_HOME unset or empty means $HOME/.config.
    static QString defaultPath();

    // Missing file: the defaults, *error left empty, returns true.
    // A file that is not well-formed or does not validate against the schema: the defaults, *error set, returns
    // false. Otherwise the file's values over the defaults, returns true.
    static bool load(const QString &path, Config *config, QString *error = nullptr);

    // Writes every element (defaults included) in the schema's namespace, version="1", creating the folder.
    // The written file validates against data/config.xsd. Returns false and sets *error when writing fails.
    bool save(const QString &path, QString *error = nullptr) const;

    // The schema's bytes, from the resource carried in the binary.
    static QByteArray schema();
};

} // namespace rileyshot
