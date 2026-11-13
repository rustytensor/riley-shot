# riley-shot: files and formats

What riley-shot reads, what it writes, and the flags that exist only for the tests.

## Installed files

`cmake --install BUILD --prefix ~/.local` installs three files and nothing else:

| File | What it is |
|---|---|
| `~/.local/bin/riley-shot` | The program. The QML and the config schema are compiled into it: it needs no other file. |
| `~/.local/share/applications/riley-shot.desktop` | The launcher entry: "Riley Shot", with the actions Capture a region, Capture a screen, Capture a window. |
| `~/.local/share/icons/hicolor/scalable/apps/riley-shot.svg` | The icon. |

The editor window's app id (Wayland) is `riley-shot`, the same as the `.desktop` file's name and its
`StartupWMClass`. The capture overlay's layer-shell namespace is `riley-shot`.

## config.xml

Path: `$XDG_CONFIG_HOME/riley-shot/config.xml`; `XDG_CONFIG_HOME` defaults to `~/.config`, so normally
`~/.config/riley-shot/config.xml`.

- The file is optional. Without it every setting has its default.
- Every element is optional and may come in any order. A missing element takes its default.
- riley-shot validates the file against its schema every time it reads it. A file that does not validate is not
  used at all: riley-shot runs on the defaults and says why on stderr.
- The schema is `data/config.xsd` in the repo, namespace `urn:riley-shot:config`. The same schema is compiled into
  the program. To check a file by hand:
  `xmllint --noout --schema data/config.xsd ~/.config/riley-shot/config.xml`.
- `data/config.example.xml` is a full file with every element at its default.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<riley-shot xmlns="urn:riley-shot:config" version="1">
  <save-folder>~/Pictures/Screenshots</save-folder>
  <file-name>Screenshot_%Y-%m-%d_%H-%M-%S</file-name>
  <copy-on-capture>true</copy-on-capture>
  <save-on-capture>true</save-on-capture>
  <pointer>false</pointer>
  <colours>
    <pen>red</pen>
    <highlighter>yellow</highlighter>
    <shape>red</shape>
    <text>red</text>
    <marker>accent</marker>
  </colours>
</riley-shot>
```

### Elements

| Element | Type | Default | Meaning |
|---|---|---|---|
| `riley-shot` `version` attribute | `1` | required | The format version. Only `1` exists. |
| `save-folder` | one line, not only spaces | `~/Pictures/Screenshots` | Where shots are saved. A leading `~` is the home folder. Created when missing. |
| `file-name` | one line, no `/`, not only spaces | `Screenshot_%Y-%m-%d_%H-%M-%S` | The file name pattern, without the extension. See the tokens below. |
| `copy-on-capture` | `true` / `false` (also `1` / `0`) | `true` | Copy each capture to the clipboard as `image/png` (with `wl-copy`). |
| `save-on-capture` | `true` / `false` | `true` | Save each capture to `save-folder` as PNG. |
| `pointer` | `true` / `false` | `false` | Where the capture bar's "include the pointer" toggle starts. |
| `colours` | element | (all defaults) | The editor's starting colour per tool, below. |
| `colours/pen` | colour | `red` | Pen. |
| `colours/highlighter` | colour | `yellow` | Highlighter. |
| `colours/shape` | colour | `red` | Line, arrow, rectangle and ellipse. |
| `colours/text` | colour | `red` | Text. |
| `colours/marker` | colour | `accent` | Numbered markers. |

A colour is either `#rrggbb` or the name of a Riley theme colour: `base`, `surface0`, `surface1`, `text`, `subtext`,
`accent`, `mauve`, `lavender`, `teal`, `peach`, `green`, `red`, `yellow` (see Theme below).

### File name pattern tokens

| Token | Becomes |
|---|---|
| `%Y` | year, 4 digits |
| `%m` | month, 2 digits |
| `%d` | day, 2 digits |
| `%H` | hour 00 to 23, 2 digits |
| `%M` | minute, 2 digits |
| `%S` | second, 2 digits |
| `%%` | a `%` |

- Any other `%x` is kept as written.
- `.png` (or `.jpg` for JPEG) is added.
- Every `/` becomes `_`, so a name never leaves the save folder.
- Example: `Screenshot_%Y-%m-%d_%H-%M-%S` at 2026-11-12 21:05:09 gives `Screenshot_2026-11-12_21-05-09.png`.
- An existing file is never overwritten: riley-shot tries `NAME-2.png`, `NAME-3.png`, and so on.

## Theme

Colours and font come from the Riley theme, `~/.config/riley/bar.json`: the file hypr-theme writes and the Riley
bar reads (`riley/bar/Theme.qml`). riley-shot uses the same keys, the same defaults and the same rule: a key present
with a valid value overrides its default; anything else keeps the default. A missing file or broken JSON means all
defaults.

| Key | Default |
|---|---|
| `base` | `#151a23` |
| `surface0` | `#1d2430` |
| `surface1` | `#283244` |
| `text` | `#f0ece0` |
| `subtext` | `#d6d2c4` |
| `accent` | `#e9a046` |
| `mauve` | `#bf98d9` |
| `lavender` | `#a0c0e8` |
| `teal` | `#68bfb5` |
| `peach` | `#e9a046` |
| `green` | `#9fc184` |
| `red` | `#e5776a` |
| `yellow` | `#e3c169` |
| `font` | `IBM Plex Mono` |

The editor's 8 colour swatches are, in order: red, peach, yellow, green, teal, lavender, mauve, text. A ninth button
opens a colour dialog for any other colour.

riley-shot never writes `bar.json`.

## Output formats

- **PNG** is the default everywhere: the capture's save, the clipboard (`image/png`), the editor's Save (Ctrl+S).
- **JPEG** (quality 92) when the editor's Save as is given a name ending in `.jpg` or `.jpeg` (any case). Any other
  name is saved as PNG.
- Blur and pixelate change the saved pixels themselves. The original pixels under them are not in the file.
- `riley-shot grab` writes PNG only.

## Clipboard

Copying runs `wl-copy --type image/png` with the PNG bytes on its standard input, never through a shell. riley-shot
waits up to 5 s for it.

## Test-only flags and variables

These exist so the gates can run riley-shot without touching the real clipboard, the real config or the whole
desktop. They are not needed for normal use.

| Flag or variable | Meaning |
|---|---|
| `--outputs NAME[,NAME...]` | Capture: freeze and cover only these outputs (the live gates use `--outputs DP-2`). An unknown name exits 4. |
| `--exit-after-ms N` | Capture and edit: give up after N ms (at most 10000) with exit code 3. In capture it limits the editor too, with a fresh timer of the same length. |
| `--report FILE` | Capture and edit: append one JSON object per line to FILE: `surface` (output, device width and height, scale) once per overlay surface, `bar` (where the capture bar is, in device pixels), `ready`, `editor` (width, height), and `exit` (code). |
| `RILEYSHOT_COPY_COMMAND` | The copy command instead of `wl-copy --type image/png` (split like a shell command line, but not run through a shell). The gates point it at `tests/fake/wl-copy`, which records its arguments and input. |
| `RILEYSHOT_HYPRCTL` | The `hyprctl` program to run instead of `hyprctl`. The gates point it at `tests/fake/hyprctl`. |
| `XDG_CONFIG_HOME`, `HOME` | Where config.xml and bar.json are read from; the gates set throwaway ones. |

## Exit codes

| Command | Codes |
|---|---|
| capture | 0 captured, 1 cancelled (Escape or the close button), 3 the `--exit-after-ms` timer ran out, 4 failure |
| edit | 0 the window was closed, 3 the timer ran out, 4 the file cannot be read |
| grab | 0 written, 1 failed |
| any | 2 for a bad command line |
