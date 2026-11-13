#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The M3 gate: the editor. Offscreen only: nothing appears on any screen and DP-2 is not touched.
#
#   tests/run-editor.sh            build, run every check, print PASS/FAIL per check and GATE PASS or GATE FAIL
#   tests/run-editor.sh --plant    copy the tree to /srv/devops/forge/build/riley-shot/plant-m3, apply
#                                  tests/faults/m3-*.patch, run the gate there and expect it to FAIL
#
# Environment:
#   RILEYSHOT_BUILD_DIR  build folder (default /srv/devops/forge/build/riley-shot/gate-m3)
#   RILEYSHOT_BIN        an installed riley-shot to use for the program checks instead of the one just built
#
# What it checks:
#   1. the build (Ninja, 8 jobs);
#   2. the Qt Test programs, offscreen with the software renderer, one line per test function:
#      tst_document   every tool on a known canvas at hand-worked pixels (pen, highlighter alpha, line, arrow head,
#                     rectangle edges, ellipse, text box, numbered markers, eraser, blur, pixelate), undo and redo to
#                     the exact pixel, move/resize/delete, the Select tool, crop sizes, save (PNG equal to the render,
#                     JPEG on request) and copy (image/png through a fake wl-copy);
#      tst_editor_qml the editor's QML on a real document: every tool button, swatches, fill, fit and zoom, drawing
#                     with the mouse, the text tool, the shortcuts (Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y, Delete, Ctrl+C,
#                     Ctrl+S) and buttons;
#      tst_editor_cli the `edit` command line; tst_cli (M1/M2 command line, unchanged);
#   3. the program (RILEYSHOT_BIN or the build's), offscreen: --help lists `edit FILE`; `edit` without a file exits 2;
#      `edit` on a missing file exits 4; `edit` on a 320x200 PNG opens the editor (the QML compiled into the binary),
#      reports {"event":"editor","width":320,"height":200} and closes by itself on --exit-after-ms 1500 with exit
#      code 3; nothing was copied or saved.
#
# Scratch images go to /srv/devops/forge/build/riley-shot/shots/m3-<pid> and are deleted on every exit. The real
# clipboard is never touched (RILEYSHOT_COPY_COMMAND and PATH point wl-copy at tests/fake/wl-copy), and riley-shot
# reads a throwaway config (XDG_CONFIG_HOME) that saves into the scratch folder.
set -u

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
FORGE=/srv/devops/forge/build/riley-shot

# ---------------------------------------------------------------------------------------------------------------
if [ "${1:-}" = "--plant" ]; then
    shopt -s nullglob
    patches=("$ROOT"/tests/faults/m3-*.patch)
    if [ ${#patches[@]} -eq 0 ]; then
        echo "PLANT: no fault patch (tests/faults/m3-*.patch); nothing to plant"
        exit 2
    fi
    PLANT=$FORGE/plant-m3
    rm -rf "$PLANT"
    mkdir -p "$PLANT/src"
    tar -C "$ROOT" --exclude=./.git --exclude=./build -cf - . | tar -C "$PLANT/src" -xf -
    for p in "${patches[@]}"; do
        if ! patch -d "$PLANT/src" -p1 --forward --batch --quiet < "$p"; then
            echo "PLANT: $(basename "$p") does not apply"
            rm -rf "$PLANT"
            exit 2
        fi
        echo "PLANT: applied $(basename "$p")"
    done
    env -u RILEYSHOT_BIN RILEYSHOT_BUILD_DIR="$PLANT/build" bash "$PLANT/src/tests/run-editor.sh"
    rc=$?
    rm -rf "$PLANT"
    if [ $rc -ne 0 ]; then
        echo "PLANT CAUGHT: the gate failed with the fault planted, as it must"
        exit 0
    fi
    echo "PLANT MISSED: the gate passed with the fault planted"
    exit 1
fi

# ---------------------------------------------------------------------------------------------------------------
BUILD=${RILEYSHOT_BUILD_DIR:-$FORGE/gate-m3}
LOGS=$BUILD/gate-logs
SHOTS=$FORGE/shots/m3-$$
mkdir -p "$BUILD" "$LOGS" "$SHOTS"
PIDS=()
FAILED=0

cleanup() {
    for pid in "${PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null
            wait "$pid" 2>/dev/null
        fi
    done
    rm -rf "$SHOTS"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; FAILED=1; }
excerpt() { tail -n 25 "$1" | sed 's/^/    | /'; }

# Never the real clipboard: a fake wl-copy first on PATH, and the copy command pointed at it.
mkdir -p "$SHOTS/bin" "$SHOTS/clipboard"
ln -s "$ROOT/tests/fake/wl-copy" "$SHOTS/bin/wl-copy"
export PATH="$SHOTS/bin:$PATH"
export RILEYSHOT_COPY_COMMAND="$ROOT/tests/fake/wl-copy --type image/png"
export FAKE_WLCOPY_DIR="$SHOTS/clipboard"
export QT_FORCE_STDERR_LOGGING=1
# Offscreen, always: no window may reach the desktop.
export QT_QPA_PLATFORM=offscreen
export QT_QUICK_BACKEND=software
unset WAYLAND_DISPLAY DISPLAY

# A throwaway config: anything saved lands in the scratch folder (deleted on exit), never in ~/Pictures.
mkdir -p "$SHOTS/config/riley-shot" "$SHOTS/saved"
cat > "$SHOTS/config/riley-shot/config.xml" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<riley-shot xmlns="urn:riley-shot:config" version="1">
  <save-folder>$SHOTS/saved</save-folder>
  <copy-on-capture>true</copy-on-capture>
  <save-on-capture>true</save-on-capture>
  <pointer>false</pointer>
</riley-shot>
EOF

# --- 1. build -------------------------------------------------------------------------------------------------
if cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo > "$LOGS/configure.log" 2>&1; then
    pass "configure"
    # -k 0: build everything that can be built, so one missing piece does not hide the rest.
    if cmake --build "$BUILD" -j8 -- -k 0 > "$LOGS/build.log" 2>&1; then
        pass "build"
    else
        fail "build (see $LOGS/build.log)"
        grep -E 'error|Error' "$LOGS/build.log" | head -n 15 | sed 's/^/    | /'
    fi
else
    fail "configure (see $LOGS/configure.log)"
    excerpt "$LOGS/configure.log"
fi

# --- 2. Qt Test programs, offscreen ---------------------------------------------------------------------------
for t in tst_document tst_editor_qml tst_editor_cli tst_cli; do
    if [ ! -x "$BUILD/$t" ]; then
        fail "$t (not built)"
        continue
    fi
    # Its own HOME and XDG_CONFIG_HOME, so nothing reads or writes Marc's files.
    HOME="$SHOTS" XDG_CONFIG_HOME="$SHOTS/config" timeout 180 "$BUILD/$t" -o "$LOGS/$t.txt,txt" \
        > "$LOGS/$t.out" 2>&1
    rc=$?
    while IFS= read -r line; do
        case "$line" in
            "PASS   : "*) name=${line#PASS   : }; pass "$t ${name#*::}" ;;
            "FAIL!  : "*) name=${line#FAIL!  : }; fail "$t ${name#*::}" ;;
            "XPASS  : "*) name=${line#XPASS  : }; fail "$t ${name#*::} (unexpected pass)" ;;
            "QWARN  : "*) echo "    | ${line#QWARN  : }" ;;
            "   Loc: "* | "   Actual "* | "   Expected "*) echo "    | ${line#   }" ;;
        esac
    done < "$LOGS/$t.txt"
    if [ $rc -eq 0 ]; then
        pass "$t (exit 0)"
    else
        fail "$t (exit $rc)"
        [ -s "$LOGS/$t.txt" ] || excerpt "$LOGS/$t.out"
    fi
done

# --- 3. the program, offscreen --------------------------------------------------------------------------------
BIN=${RILEYSHOT_BIN:-$BUILD/riley-shot}
echo "     (riley-shot under test: $BIN)"

# Waits up to $2 tenths of a second for PID $1 to exit; then reaps it and sets RC to its exit code. On timeout,
# kills it, sets RC=timeout and returns 1.
wait_exit() {
    local pid=$1 tenths=$2
    for _ in $(seq "$tenths"); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.1
    done
    if kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null
        wait "$pid" 2>/dev/null
        RC=timeout
        return 1
    fi
    wait "$pid" 2>/dev/null
    RC=$?
    return 0
}

# run_rs LABEL TENTHS ARGS...: riley-shot ARGS in the background with the test config, saved PID, output to
# $LOGS/rs-LABEL.out; waits up to TENTHS tenths of a second. Sets RC.
run_rs() {
    local label=$1 tenths=$2
    shift 2
    HOME="$SHOTS" XDG_CONFIG_HOME="$SHOTS/config" "$BIN" "$@" > "$LOGS/rs-$label.out" 2>&1 &
    local pid=$!
    PIDS+=("$pid")
    wait_exit "$pid" "$tenths"
}

if [ -x "$BIN" ]; then
    run_rs help 50 --help
    if [ "$RC" = 0 ] && grep -q 'edit FILE' "$LOGS/rs-help.out"; then
        pass "riley-shot --help exits 0 and lists 'edit FILE'"
    else
        fail "riley-shot --help: exit $RC, want 0 with 'edit FILE' in the usage"
    fi

    run_rs nofile 50 edit
    [ "$RC" = 2 ] && pass "riley-shot edit (no file) exits 2" || fail "riley-shot edit (no file) exits $RC, want 2"

    run_rs missing 50 edit "$SHOTS/missing.png" --exit-after-ms 3000 --report "$SHOTS/report-missing.jsonl"
    [ "$RC" = 4 ] && pass "riley-shot edit on a missing file exits 4" \
        || fail "riley-shot edit on a missing file exits $RC, want 4"

    # A 320x200 PNG made here (no private pixels): a horizontal ramp, written with Python's zlib.
    python3 -I - "$SHOTS/in.png" <<'EOF'
import struct, sys, zlib
w, h = 320, 200
raw = b"".join(b"\x00" + bytes(v for x in range(w) for v in (x * 255 // (w - 1), 128, 255 - x * 255 // (w - 1)))
               for _ in range(h))
def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) \
      + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
EOF
    # The editor opens, shows the image, and its timer closes it: exit 3 within 6 s.
    run_rs editor 60 edit "$SHOTS/in.png" --exit-after-ms 1500 --report "$SHOTS/report.jsonl"
    if [ "$RC" = 3 ]; then
        pass "riley-shot edit closes by itself on --exit-after-ms 1500 (exit 3)"
    else
        fail "riley-shot edit exits $RC, want 3 (timeout means it was still running after 6 s and was killed)"
        excerpt "$LOGS/rs-editor.out"
    fi
    report=$(python3 -I - "$SHOTS/report.jsonl" <<'EOF'
import json, sys
editors, exits = [], []
try:
    lines = open(sys.argv[1]).read().splitlines()
except OSError:
    lines = []
for line in lines:
    try:
        e = json.loads(line)
    except Exception:
        print("FAIL unparsable report line: " + line.strip()[:80])
        continue
    if e.get("event") == "editor":
        editors.append(e)
    elif e.get("event") == "exit":
        exits.append(e)
if len(editors) == 1 and (editors[0].get("width"), editors[0].get("height")) == (320, 200):
    print("PASS one editor line, 320x200")
else:
    print("FAIL editor lines %s, want exactly one of 320x200" % [(e.get("width"), e.get("height")) for e in editors])
if len(exits) == 1 and exits[0].get("code") == 3:
    print("PASS one exit line, code 3")
else:
    print("FAIL exit lines %s, want exactly one with code 3" % [e.get("code") for e in exits])
EOF
)
    while IFS= read -r line; do
        case "$line" in
            PASS*) pass "edit report: ${line#PASS }" ;;
            FAIL*) fail "edit report: ${line#FAIL }" ;;
        esac
    done <<< "$report"

    if [ -z "$(ls -A "$SHOTS/saved")" ] && [ ! -e "$SHOTS/clipboard/args" ]; then
        pass "nothing was copied or saved by opening the editor"
    else
        fail "opening the editor copied or saved something"
    fi
else
    fail "riley-shot (not built: $BIN)"
fi

# ---------------------------------------------------------------------------------------------------------------
if [ $FAILED -eq 0 ]; then
    echo "GATE PASS"
    exit 0
fi
echo "GATE FAIL"
exit 1
