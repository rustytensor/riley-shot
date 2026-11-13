# riley-shot

Screenshots for the Riley desktop on Hyprland: a capture bar (region, screen or window) drawn on every screen at that
screen's own scale, then an editor window to draw on the shot (pen, highlighter, arrows, shapes, text, crop, blur)
before copying or saving it. C++ and Qt Quick.

Every screen is frozen at once, each from its own pixels (`grim -o` per output), and each gets its own full-screen
overlay at 1:1 device pixels. So it stays aligned on desktops that mix scales (1, 1.25, 1.6) and rotated screens.

## Usage

```
riley-shot [capture] [--region | --screen | --window] [--no-editor]
riley-shot edit FILE
riley-shot grab --output NAME --out FILE.png [--pointer]
riley-shot --help
```

| Command | What it does |
|---|---|
| `riley-shot` or `riley-shot capture` | Freeze every screen and show the capture bar on the screen under the pointer. |
| `riley-shot --region` | Skip the bar's mode choice: go straight to drawing a region. |
| `riley-shot --screen` | Go straight to picking a screen. |
| `riley-shot --window` | Go straight to picking a window. |
| `... --no-editor` | Copy and save only; do not open the editor. |
| `riley-shot edit FILE` | Open an existing image in the editor. |
| `riley-shot grab --output NAME --out FILE.png` | Write one output's pixels to a PNG, no UI. `--pointer` includes the pointer. |

After a capture, riley-shot copies the shot to the clipboard and saves it to `~/Pictures/Screenshots` (both can be
turned off in the config), then opens the editor.

Exit codes: 0 done, 1 cancelled, 2 bad command line, 3 a test timer ran out, 4 failure. The test-only flags
(`--outputs`, `--exit-after-ms`, `--report`) are described in [docs/files.md](docs/files.md).

### Capture

The bar: Region, Screen, Window; a toggle to include the pointer; the round capture button; close.

| Input | Does |
|---|---|
| Region: drag | Draw a rectangle, on any screen or across screens. Its size shows in pixels. |
| Region: drag a corner or edge handle | Resize it. Drag inside it to move it. |
| Arrow keys | Move the region by 1 pixel. |
| Shift + arrow keys | Resize the region by 1 pixel. |
| Screen: click a screen | Capture that screen. |
| Window: hover, click | Windows light up under the pointer; a click captures one. |
| Enter, or the capture button | Capture. |
| Escape, or the close button | Cancel. |

A region across screens is cut from each screen's own pixels and joined at the highest scale among them.

### Editor

Tools in the top toolbar: select, pen, highlighter, eraser, line, arrow, rectangle, ellipse (outline or filled), text,
numbered markers, crop, blur, pixelate. Then 8 theme colours plus a custom colour, the stroke width, undo and redo,
copy, save, save as, and zoom.

Annotations stay objects until saved: with the select tool they can be moved, resized and deleted.

| Key | Does |
|---|---|
| Ctrl+Z | Undo |
| Ctrl+Shift+Z, Ctrl+Y | Redo |
| Ctrl+C | Copy the image (PNG) to the clipboard |
| Ctrl+S | Save as PNG in the save folder, named by the file name pattern |
| Ctrl+Shift+S | Save as... (a name ending in `.jpg` or `.jpeg` saves JPEG) |
| Delete, Backspace | Delete the selected annotation |
| Escape | Clear the selection |
| Ctrl+= or Ctrl++, Ctrl+- | Zoom in, zoom out |
| Ctrl+0 | Fit to the window |
| Text tool: Enter, Escape | Place the text, or drop it |

Blur and pixelate change the saved pixels themselves: nothing under them can be recovered from the file.

## Configuration

`~/.config/riley-shot/config.xml` (optional), checked against the schema `data/config.xsd`: the save folder, the file
name pattern, copy and save on capture, the pointer default and the editor's starting colours.
`data/config.example.xml` has every setting at its default. Colours and the font come from the Riley theme,
`~/.config/riley/bar.json`. Every element, default and token is in [docs/files.md](docs/files.md).

## Build and install

Needs Qt 6.11 (Core, Gui, Qml, Quick, Test), layer-shell-qt 6.8, libxml2, CMake 3.24 and Ninja; at run time `grim`,
`wl-copy` and `hyprctl`.

```
cmake -S . -B /path/to/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build /path/to/build
cmake --install /path/to/build --prefix ~/.local
```

The build folder must be outside the source tree. Install puts the program in `~/.local/bin/riley-shot` (the QML and
the schema are compiled into it), `riley-shot.desktop` in `~/.local/share/applications` and the icon in
`~/.local/share/icons/hicolor/scalable/apps`.

## Tests

Each milestone has a gate script that prints PASS or FAIL per check and ends in GATE PASS or GATE FAIL:

| Script | Checks |
|---|---|
| `tests/run-capture.sh` | Geometry for a five-screen, three-scale layout; a live grab of DP-2 against a known pattern. |
| `tests/run-overlay.sh` | The capture bar and region logic offscreen; the live overlay on DP-2 matches the screen pixel for pixel. |
| `tests/run-editor.sh` | Every editor tool at hand-worked pixels, undo and redo, save and copy; offscreen. |
| `tests/run-install.sh` | Installs to `~/.local`, checks the installed files, and runs the three gates above on the installed program. |

`--plant` on any of them applies the matching fault from `tests/faults/` in a scratch copy and expects GATE FAIL.
The live parts touch only the output DP-2, and never the real clipboard.

## Licence

GPL-3.0-or-later. See `LICENSE`.
