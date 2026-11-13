#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The M1 gate: capture core, no UI.
#
#   tests/run-capture.sh            build, run every check, print PASS/FAIL per check and GATE PASS or GATE FAIL
#   tests/run-capture.sh --plant    copy the tree to /srv/devops/forge/build/riley-shot/plant-m1, apply
#                                   tests/faults/m1-*.patch, run the gate there and expect it to FAIL
#
# Environment:
#   RILEYSHOT_BUILD_DIR  build folder (default /srv/devops/forge/build/riley-shot/gate-m1)
#   RILEYSHOT_BIN        an installed riley-shot to use for the live checks instead of the one just built
#
# What it checks:
#   1. the build (Ninja, 8 jobs);
#   2. the Qt Test programs: tst_geometry (hand-worked geometry for the five outputs), tst_grab (fake grim),
#      tst_config (config.xml and data/config.xsd, validated with xmllint), tst_theme, tst_deliver (fake wl-copy),
#      tst_cli; one line per test function;
#   3. data/config.xsd against the shipped example and a bad file, with xmllint;
#   4. riley-shot's exit codes for --help (0) and an unknown option (2);
#   5. live, on DP-2 only: a test window (rileyshot-testpattern) draws a known pattern at DP-2's device pixels,
#      `riley-shot grab --output DP-2` grabs it through the capture core, and rileyshot-checkpattern checks the
#      colours at known device pixels, including 1-device-pixel features between logical pixels (scale 1.25).
#      The test window closes by itself after 8 s and the gate kills it by its saved PID well before 10 s.
#
# Screenshots go to /srv/devops/forge/build/riley-shot/shots/m1-<pid> and are deleted on every exit. The real
# clipboard is never touched: RILEYSHOT_COPY_COMMAND and PATH point wl-copy at tests/fake/wl-copy.
set -u

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
FORGE=/srv/devops/forge/build/riley-shot

# ---------------------------------------------------------------------------------------------------------------
if [ "${1:-}" = "--plant" ]; then
    shopt -s nullglob
    patches=("$ROOT"/tests/faults/m1-*.patch)
    if [ ${#patches[@]} -eq 0 ]; then
        echo "PLANT: no fault patch (tests/faults/m1-*.patch); nothing to plant"
        exit 2
    fi
    PLANT=$FORGE/plant-m1
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
    env -u RILEYSHOT_BIN RILEYSHOT_BUILD_DIR="$PLANT/build" bash "$PLANT/src/tests/run-capture.sh"
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
BUILD=${RILEYSHOT_BUILD_DIR:-$FORGE/gate-m1}
LOGS=$BUILD/gate-logs
SHOTS=$FORGE/shots/m1-$$
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
mkdir -p "$SHOTS/bin"
ln -s "$ROOT/tests/fake/wl-copy" "$SHOTS/bin/wl-copy"
mkdir -p "$SHOTS/clipboard"
export PATH="$SHOTS/bin:$PATH"
export RILEYSHOT_COPY_COMMAND="$ROOT/tests/fake/wl-copy --type image/png"
export FAKE_WLCOPY_DIR="$SHOTS/clipboard"

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

# --- 2. Qt Test programs --------------------------------------------------------------------------------------
for t in tst_geometry tst_grab tst_config tst_theme tst_deliver tst_cli; do
    if [ ! -x "$BUILD/$t" ]; then
        fail "$t (not built)"
        continue
    fi
    QT_QPA_PLATFORM=offscreen timeout 120 "$BUILD/$t" -o "$LOGS/$t.txt,txt" > /dev/null 2>&1
    rc=$?
    # One line per test function (and data row), as Qt Test reports them.
    while IFS= read -r line; do
        case "$line" in
            "PASS   : "*) name=${line#PASS   : }; pass "$t ${name#*::}" ;;
            "FAIL!  : "*) name=${line#FAIL!  : }; fail "$t ${name#*::}" ;;
            "XPASS  : "*) name=${line#XPASS  : }; fail "$t ${name#*::} (unexpected pass)" ;;
            "   Loc: "* | "   Actual "* | "   Expected "*) echo "    | ${line#   }" ;;
        esac
    done < "$LOGS/$t.txt"
    if [ $rc -eq 0 ]; then
        pass "$t (exit 0)"
    else
        fail "$t (exit $rc)"
    fi
done

# --- 3. the schema, by xmllint alone ---------------------------------------------------------------------------
if xmllint --noout --schema "$ROOT/data/config.xsd" "$ROOT/data/config.example.xml" > "$LOGS/xsd.log" 2>&1; then
    pass "xmllint: data/config.example.xml validates against data/config.xsd"
else
    fail "xmllint: data/config.example.xml does not validate"
    excerpt "$LOGS/xsd.log"
fi
if xmllint --noout --schema "$ROOT/data/config.xsd" "$ROOT/tests/data/config-bad.xml" > "$LOGS/xsd-bad.log" 2>&1; then
    fail "xmllint: tests/data/config-bad.xml validates but must not"
else
    pass "xmllint: tests/data/config-bad.xml is rejected"
fi

# --- 4. the command line --------------------------------------------------------------------------------------
BIN=${RILEYSHOT_BIN:-$BUILD/riley-shot}
echo "     (riley-shot under test: $BIN)"
if [ -x "$BIN" ]; then
    timeout 5 "$BIN" --help > "$LOGS/cli-help.log" 2>&1
    rc=$?
    [ $rc -eq 0 ] && pass "riley-shot --help exits 0" || fail "riley-shot --help exits $rc, want 0"
    timeout 5 "$BIN" --bogus-option > "$LOGS/cli-bogus.log" 2>&1
    rc=$?
    [ $rc -eq 2 ] && pass "riley-shot --bogus-option exits 2" || fail "riley-shot --bogus-option exits $rc, want 2"
else
    fail "riley-shot (not built: $BIN)"
fi

# --- 5. live, DP-2 only ---------------------------------------------------------------------------------------
# DP-2 must still be what the hand-worked values assume: mode 2560x1600 at 1440,1847, scale 1.25, transform 0.
live_ok=1
if [ -z "${WAYLAND_DISPLAY:-}" ] || ! hyprctl monitors -j > "$SHOTS/monitors.json" 2> "$LOGS/hyprctl.log"; then
    fail "live: needs the running Hyprland session (WAYLAND_DISPLAY and hyprctl)"
    live_ok=0
elif python3 -I - "$SHOTS/monitors.json" <<'EOF'
import json, sys
m = [o for o in json.load(open(sys.argv[1])) if o["name"] == "DP-2"]
ok = len(m) == 1 and (m[0]["width"], m[0]["height"], m[0]["x"], m[0]["y"], m[0]["scale"], m[0]["transform"]) \
    == (2560, 1600, 1440, 1847, 1.25, 0)
sys.exit(0 if ok else 1)
EOF
then
    pass "live: DP-2 is 2560x1600 at 1440,1847 scale 1.25 transform 0"
else
    fail "live: DP-2 is not 2560x1600 at 1440,1847 scale 1.25; the live expectations no longer hold"
    live_ok=0
fi
for helper in rileyshot-testpattern rileyshot-checkpattern; do
    if [ ! -x "$BUILD/$helper" ]; then
        fail "live: $helper (not built)"
        live_ok=0
    fi
done
[ -x "$BIN" ] || live_ok=0

if [ $live_ok -eq 1 ]; then
    "$BUILD/rileyshot-testpattern" --output DP-2 --close-after 8000 > "$SHOTS/pattern.out" 2> "$LOGS/pattern.err" &
    PATTERN=$!
    PIDS+=("$PATTERN")
    # Up to 3 s for the first frame.
    for _ in $(seq 30); do
        grep -q -E '^(READY|BADBUFFER)' "$SHOTS/pattern.out" 2>/dev/null && break
        kill -0 "$PATTERN" 2>/dev/null || break
        sleep 0.1
    done
    state=$(head -n 1 "$SHOTS/pattern.out" 2>/dev/null)
    case "$state" in
        READY*)
            pass "live: test window on DP-2 ($state)"
            # Let a layer open animation finish before grabbing.
            sleep 1.5
            timeout 4 "$BIN" grab --output DP-2 --out "$SHOTS/dp2.png" > "$LOGS/grab.log" 2>&1
            rc=$?
            if [ $rc -eq 0 ] && [ -s "$SHOTS/dp2.png" ]; then
                pass "live: riley-shot grab --output DP-2"
            else
                fail "live: riley-shot grab --output DP-2 exits $rc"
                excerpt "$LOGS/grab.log"
            fi
            ;;
        BADBUFFER*)
            fail "live: the test window cannot draw 1:1 on DP-2 ($state); a test fault, not riley-shot's"
            ;;
        *)
            fail "live: the test window did not show within 3 s"
            excerpt "$LOGS/pattern.err"
            ;;
    esac
    if kill -0 "$PATTERN" 2>/dev/null; then
        kill "$PATTERN" 2>/dev/null
    fi
    wait "$PATTERN" 2>/dev/null
    if [ -s "$SHOTS/dp2.png" ]; then
        QT_QPA_PLATFORM=offscreen "$BUILD/rileyshot-checkpattern" "$SHOTS/dp2.png" > "$LOGS/checkpattern.log" 2>&1
        rc=$?
        while IFS= read -r line; do
            case "$line" in
                PASS*) pass "live: ${line#PASS }" ;;
                FAIL*) fail "live: ${line#FAIL }" ;;
            esac
        done < "$LOGS/checkpattern.log"
        [ $rc -eq 0 ] || fail "live: rileyshot-checkpattern exits $rc"
    fi
fi

# ---------------------------------------------------------------------------------------------------------------
if [ $FAILED -eq 0 ]; then
    echo "GATE PASS"
    exit 0
fi
echo "GATE FAIL"
exit 1
