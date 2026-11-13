// SPDX-License-Identifier: GPL-3.0-or-later
// The Riley theme: colours and font from ~/.config/riley/bar.json, the file hypr-theme writes and the bar reads
// (riley bar/Theme.qml). Same keys, same defaults, same rule: each key present with a valid value overrides its
// default; anything else keeps the default.
//
// Owner: builder B (theme.cpp). The declarations below are the contract the M1 gate tests; do not change them.
#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

namespace rileyshot {

struct Theme
{
    // Defaults: riley bar/Theme.qml.
    QColor base{0x15, 0x1a, 0x23};      // #151a23
    QColor surface0{0x1d, 0x24, 0x30};  // #1d2430
    QColor surface1{0x28, 0x32, 0x44};  // #283244
    QColor text{0xf0, 0xec, 0xe0};      // #f0ece0
    QColor subtext{0xd6, 0xd2, 0xc4};   // #d6d2c4
    QColor accent{0xe9, 0xa0, 0x46};    // #e9a046
    QColor mauve{0xbf, 0x98, 0xd9};     // #bf98d9
    QColor lavender{0xa0, 0xc0, 0xe8};  // #a0c0e8
    QColor teal{0x68, 0xbf, 0xb5};      // #68bfb5
    QColor peach{0xe9, 0xa0, 0x46};     // #e9a046
    QColor green{0x9f, 0xc1, 0x84};     // #9fc184
    QColor red{0xe5, 0x77, 0x6a};       // #e5776a
    QColor yellow{0xe3, 0xc1, 0x69};    // #e3c169
    QString font = QStringLiteral("IBM Plex Mono");
    bool operator==(const Theme &) const = default;

    // The colour keys, in this order: base surface0 surface1 text subtext accent mauve lavender teal peach green red
    // yellow.
    static QStringList keys();

    // $HOME/.config/riley/bar.json (the bar reads exactly this path).
    static QString defaultPath();

    // Missing file or broken JSON: the defaults. Otherwise each key above (and "font") present as a valid colour
    // (or, for font, a non-empty string) overrides its default.
    static Theme load(const QString &path);

    // The colour for a key; an invalid QColor for an unknown key.
    QColor colour(const QString &key) const;
    // A colour spec as config.xml writes it: "#rrggbb" or a key. Invalid QColor when it is neither.
    QColor resolve(const QString &spec) const;
    // The editor's 8 swatches, in this order: red, peach, yellow, green, teal, lavender, mauve, text.
    QList<QColor> palette() const;
};

} // namespace rileyshot
