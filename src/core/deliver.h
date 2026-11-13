// SPDX-License-Identifier: GPL-3.0-or-later
// What happens to a finished shot: copy to the clipboard and save by the file name pattern.
//
// Owner: builder B (deliver.cpp). The declarations below are the contract the M1 gate tests; do not change them.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QString>
#include <QStringList>

namespace rileyshot {

// {"wl-copy", "--type", "image/png"}.
QStringList defaultCopyCommand();

// $RILEYSHOT_COPY_COMMAND split with QProcess::splitCommand when set and non-empty, else defaultCopyCommand().
// The gate sets it to a fake wl-copy so tests never touch the real clipboard.
QStringList copyCommandFromEnvironment();

// Runs command[0] with command[1..] and writes the image as PNG bytes to its stdin, then closes stdin and waits up to
// 5 s for it to exit. True when it exits 0. Never goes through a shell.
bool copyImage(const QImage &image, const QStringList &command, QString *error = nullptr);

// The file name for a pattern at a time, plus "." + extension. Tokens:
//   %Y year, 4 digits   %m month, 2   %d day, 2   %H hour (00-23), 2   %M minute, 2   %S second, 2   %% a "%".
// Any other "%x" is kept as written. Every "/" becomes "_", so a pattern never leaves the save folder.
// Example: ("Screenshot_%Y-%m-%d_%H-%M-%S", 2026-11-12 21:05:09, "png") -> "Screenshot_2026-11-12_21-05-09.png".
QString fileNameFor(const QString &pattern, const QDateTime &time, const QString &extension);

// Saves the image in folder (created when missing) as fileNameFor(pattern, time, ext), ext "png" for format "png"
// and "jpg" for "jpg"/"jpeg". When that file exists already, tries "<name>-2.<ext>", "<name>-3.<ext>", ... and never
// overwrites. Returns the full path written, or an empty string with *error set.
QString saveImage(const QImage &image, const QString &folder, const QString &pattern, const QDateTime &time,
                  const QByteArray &format = "png", QString *error = nullptr);

} // namespace rileyshot
