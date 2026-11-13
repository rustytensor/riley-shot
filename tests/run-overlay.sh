#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The M2 gate: the capture overlay and bar.
#
#   tests/run-overlay.sh            build, run every check, print PASS/FAIL per check and GATE PASS or GATE FAIL
#   tests/run-overlay.sh --plant    copy the tree to /srv/devops/forge/build/riley-shot/plant-m2, apply
#                                   tests/faults/m2-*.patch, run the gate there and expect it to FAIL
#
# Environment:
#   RILEYSHOT_BUILD_DIR  build folder (default /srv/devops/forge/build/riley-shot/gate-m2)
#   RILEYSHOT_BIN        an installed riley-shot to use for the live checks instead of the one just built
#
# What it checks:
#   1. the build (Ninja, 8 jobs);
#   2. the Qt Test programs, offscreen: tst_selection (region drag, handles, keyboard steps, size label, window
#      hit-testing, the bar's output: hand-worked for this desktop's five outputs), tst_controller (the capture
#      controller as the QML drives it, with made-up grabs and a fake wl-copy), tst_overlay_qml (the QML overlay
#      and bar on a real controller, at DP-2's scale 1.25: sizes, 1:1 pixels, buttons, drag, keys, highlights),
#      tst_cli (with the M2 test options); one line per test function;
#   3. riley-shot's exit codes for --help (0) and a bad --exit-after-ms (2);
#   4. live, on DP-2 only (every overlay here is limited with --outputs DP-2 and closes by itself well within 10 s;
#      the gate also kills each by its saved PID):
#      a. an unknown --outputs name fails with exit 4 and shows nothing;
#      b. the M1 test window (rileyshot-testpattern) holds DP-2 still; `grim -o DP-2` grabs it; then
#         `riley-shot --outputs DP-2` opens its overlay. Its report must list exactly one surface, on DP-2, of
#         2560x1600 device pixels at scale 1.25; `hyprctl layers -j` must show exactly one "riley-shot" layer,
#         on DP-2, in the overlay level, at 1440,1847 sized 2048x1280 (DP-2's logical size, 2560/1.25 x 1600/1.25),
#         owned by riley-shot's PID; a second `grim -o DP-2` under the overlay must equal the first at every
#         device pixel outside the capture bar (rileyshot-comparegrab);
#      c. `wtype -k Escape` closes it: the process exits within 2 s with code 1 (cancelled) and the layer is gone;
#      d. with nothing pressed, --exit-after-ms 1500 closes it by itself with code 3.
#
# Screenshots go to /srv/devops/forge/build/riley-shot/shots/m2-<pid> and are deleted on every exit. The real
# clipboard is never touched (RILEYSHOT_COPY_COMMAND and PATH point wl-copy at tests/fake/wl-copy), and riley-shot
# reads a throwaway config (XDG_CONFIG_HOME) that saves into the shots folder.
set -u

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
FORGE=/srv/devops/forge/build/riley-shot

# ---------------------------------------------------------------------------------------------------------------
if [ "${1:-}" = "--plant" ]; then
    shopt -s nullglob
    patches=("$ROOT"/tests/faults/m2-*.patch)
    if [ ${#patches[@]} -eq 0 ]; then
        echo "PLANT: no fault patch (tests/faults/m2-*.patch); nothing to plant"
        exit 2
    fi
    PLANT=$FORGE/plant-m2
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
    env -u RILEYSHOT_BIN RILEYSHOT_BUILD_DIR="$PLANT/build" bash "$PLANT/src/tests/run-overlay.sh"
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
BUILD=${RILEYSHOT_BUILD_DIR:-$FORGE/gate-m2}
LOGS=$BUILD/gate-logs
SHOTS=$FORGE/shots/m2-$$
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
# Qt sends its messages to the journal when stderr is not a terminal; the logs here need them.
export QT_FORCE_STDERR_LOGGING=1

# A throwaway config: should anything get captured, it lands in the shots folder (deleted on exit), never in
# ~/Pictures. Pointer off, so riley-shot's frozen grab has no pointer drawn in, like the gate's own grim grab.
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
for t in tst_selection tst_controller tst_overlay_qml tst_cli; do
    if [ ! -x "$BUILD/$t" ]; then
        fail "$t (not built)"
        continue
    fi
    QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software timeout 120 "$BUILD/$t" -o "$LOGS/$t.txt,txt" \
        > "$LOGS/$t.out" 2>&1
    rc=$?
    # One line per test function (and data row), as Qt Test reports them.
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

# --- 3. the command line --------------------------------------------------------------------------------------
BIN=${RILEYSHOT_BIN:-$BUILD/riley-shot}
echo "     (riley-shot under test: $BIN)"
if [ -x "$BIN" ]; then
    timeout 5 "$BIN" --help > "$LOGS/cli-help.log" 2>&1
    rc=$?
    [ $rc -eq 0 ] && pass "riley-shot --help exits 0" || fail "riley-shot --help exits $rc, want 0"
    timeout 5 "$BIN" --exit-after-ms 0 > "$LOGS/cli-bad-timer.log" 2>&1
    rc=$?
    [ $rc -eq 2 ] && pass "riley-shot --exit-after-ms 0 exits 2" || fail "riley-shot --exit-after-ms 0 exits $rc, want 2"
else
    fail "riley-shot (not built: $BIN)"
fi

# --- 4. live, DP-2 only ---------------------------------------------------------------------------------------
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
for helper in rileyshot-testpattern rileyshot-comparegrab; do
    if [ ! -x "$BUILD/$helper" ]; then
        fail "live: $helper (not built)"
        live_ok=0
    fi
done
for tool in grim wtype; do
    command -v "$tool" > /dev/null || { fail "live: $tool is not installed"; live_ok=0; }
done
[ -x "$BIN" ] || live_ok=0

# start_rileyshot LABEL ARGS...: runs riley-shot ARGS in the background with the test config, output to
# $LOGS/rs-LABEL.out; sets RS to its PID (the program itself, no wrapper).
start_rileyshot() {
    local label=$1
    shift
    XDG_CONFIG_HOME="$SHOTS/config" "$BIN" "$@" > "$LOGS/rs-$label.out" 2>&1 &
    RS=$!
    PIDS+=("$RS")
}

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

# Prints the riley-shot layers as "monitor level x y w h alpha pid", one per line.
rs_layers() {
    hyprctl layers -j 2> /dev/null | python3 -I -c '
import json, sys
try:
    d = json.load(sys.stdin)
except Exception:
    sys.exit(0)
for mon, v in d.items():
    for level, layers in v.get("levels", {}).items():
        for l in layers:
            if l.get("namespace") == "riley-shot":
                a = l.get("alpha", 1)
                print(mon, level, l["x"], l["y"], l["w"], l["h"], 1 if a == 1 else a, l.get("pid", -1))
'
}

# The live part holds the shared DP-2 lock (the same flock file and holder line as dp2-lock, which cannot wrap a
# block of this script), taken here and released right after the live part; the build above never holds it.
DP2_LOCK="/run/user/$(id -u)/dp2-test.lock"
if [ $live_ok -eq 1 ]; then
    exec 9> "$DP2_LOCK"
    if ! flock -n 9; then
        echo "     (waiting for the DP-2 lock: $(cat "$DP2_LOCK.holder" 2>/dev/null || echo 'another test'))"
        flock 9
    fi
    printf '%s  pid %s  %s\n' "$(date '+%F %T')" "$$" "riley-shot tests/run-overlay.sh (live part)" > "$DP2_LOCK.holder"
fi

if [ $live_ok -eq 1 ]; then
    # a. An output that does not exist: exit 4, no surface.
    start_rileyshot nope --outputs NOPE-9 --exit-after-ms 3000 --report "$SHOTS/report-nope.jsonl" --no-editor
    if wait_exit "$RS" 40; then
        [ "$RC" = 4 ] && pass "live: --outputs NOPE-9 exits 4" || fail "live: --outputs NOPE-9 exits $RC, want 4"
    else
        fail "live: --outputs NOPE-9 did not exit within 4 s"
    fi
    if grep -q '"surface"' "$SHOTS/report-nope.jsonl" 2>/dev/null; then
        fail "live: --outputs NOPE-9 showed a surface"
    else
        pass "live: --outputs NOPE-9 showed no surface"
    fi

    # b. The overlay on DP-2 over the still test pattern.
    "$BUILD/rileyshot-testpattern" --output DP-2 --close-after 9000 > "$SHOTS/pattern.out" 2> "$LOGS/pattern.err" &
    PATTERN=$!
    PIDS+=("$PATTERN")
    for _ in $(seq 30); do
        grep -q -E '^(READY|BADBUFFER)' "$SHOTS/pattern.out" 2>/dev/null && break
        kill -0 "$PATTERN" 2>/dev/null || break
        sleep 0.1
    done
    state=$(head -n 1 "$SHOTS/pattern.out" 2>/dev/null)
    case "$state" in
        READY*) pass "live: test pattern on DP-2 ($state)" ;;
        *) fail "live: the test pattern did not show on DP-2 within 3 s (${state:-nothing})"; excerpt "$LOGS/pattern.err" ;;
    esac
    # Let its layer open animation finish, then grab DP-2 as it is before riley-shot.
    sleep 1.5
    if [[ $state == READY* ]] && timeout 4 grim -o DP-2 "$SHOTS/before.png" 2> "$LOGS/grim-before.log" \
        && [ -s "$SHOTS/before.png" ]; then
        pass "live: grim -o DP-2 before the overlay"
        start_rileyshot main --outputs DP-2 --exit-after-ms 7000 --report "$SHOTS/report.jsonl" --no-editor
        RSMAIN=$RS
        # Up to 4 s for every surface to show.
        for _ in $(seq 40); do
            grep -q '"ready"' "$SHOTS/report.jsonl" 2>/dev/null && break
            kill -0 "$RSMAIN" 2>/dev/null || break
            sleep 0.1
        done
        if grep -q '"ready"' "$SHOTS/report.jsonl" 2>/dev/null; then
            pass "live: riley-shot reports ready"
            # The report: exactly one surface, DP-2, 2560x1600 device pixels at 1.25; the bar on DP-2.
            report=$(python3 -I - "$SHOTS/report.jsonl" <<'EOF'
import json, sys
surfaces, bars = [], []
for line in open(sys.argv[1]):
    try:
        e = json.loads(line)
    except Exception:
        print("BAD unparsable report line: " + line.strip()[:80])
        continue
    if e.get("event") == "surface":
        surfaces.append(e)
    elif e.get("event") == "bar":
        bars.append(e)
if len(surfaces) == 1 and surfaces[0].get("output") == "DP-2":
    print("PASS one surface, on DP-2")
else:
    print("FAIL surfaces: %s, want exactly one on DP-2" % [s.get("output") for s in surfaces])
for s in surfaces:
    size = (s.get("width"), s.get("height"), s.get("scale"))
    ok = size == (2560, 1600, 1.25)
    print("%s surface %s is %sx%s device pixels at scale %s (want 2560x1600 at 1.25)"
          % ("PASS" if ok else "FAIL", s.get("output"), *size))
if len(bars) == 1 and bars[0].get("output") == "DP-2":
    b = bars[0]
    print("PASS capture bar on DP-2 at %s,%s %sx%s" % (b["x"], b["y"], b["width"], b["height"]))
    # The bar's rect grown by 20 device pixels (16 logical at 1.25) for its shadow, clipped to the output.
    x0, y0 = max(0, b["x"] - 20), max(0, b["y"] - 20)
    x1, y1 = min(2560, b["x"] + b["width"] + 20), min(1600, b["y"] + b["height"] + 20)
    print("SKIP %d,%d,%d,%d" % (x0, y0, x1 - x0, y1 - y0))
else:
    print("FAIL bar lines: %s, want exactly one on DP-2" % [b.get("output") for b in bars])
EOF
)
            skip=""
            while IFS= read -r line; do
                case "$line" in
                    PASS*) pass "live: report: ${line#PASS }" ;;
                    FAIL* | BAD*) fail "live: report: ${line#* }" ;;
                    SKIP*) skip=${line#SKIP } ;;
                esac
            done <<< "$report"

            # The compositor's view: one riley-shot layer, DP-2, overlay level (3), 1440,1847 2048x1280, our PID.
            # Wait (up to 2 s) for the layer to reach full opacity, in case Hyprland fades layers in.
            layers=""
            for _ in $(seq 20); do
                layers=$(rs_layers)
                [ "$(printf '%s\n' "$layers" | grep -c .)" = 1 ] && [ "$(printf '%s' "$layers" | awk '{print $7}')" = 1 ] && break
                sleep 0.1
            done
            want="DP-2 3 1440 1847 2048 1280 1 $RSMAIN"
            if [ "$layers" = "$want" ]; then
                pass "live: hyprctl layers: one riley-shot layer, DP-2, overlay level, 1440,1847 2048x1280"
            else
                fail "live: hyprctl layers: got [$(printf '%s' "$layers" | tr '\n' ';')], want [$want] (monitor level x y w h alpha pid)"
            fi

            # Pixel for pixel: grab DP-2 under the overlay and compare with the grab from before. A layer animation
            # may still be running: up to 4 tries 0.5 s apart, the first match counts.
            sleep 0.5
            if [ -z "$skip" ]; then
                fail "live: no capture bar rectangle to leave out; comparing nothing"
            else
                matched=0
                for try in 1 2 3 4; do
                    if timeout 4 grim -o DP-2 "$SHOTS/during.png" 2> "$LOGS/grim-during.log" \
                        && "$BUILD/rileyshot-comparegrab" "$SHOTS/before.png" "$SHOTS/during.png" 2560 1600 "$skip" \
                            > "$LOGS/compare.log" 2>&1; then
                        matched=1
                        break
                    fi
                    kill -0 "$RSMAIN" 2>/dev/null || break
                    sleep 0.5
                done
                while IFS= read -r line; do
                    case "$line" in
                        PASS*) pass "live: compare: ${line#PASS }" ;;
                        FAIL*) fail "live: compare: ${line#FAIL }" ;;
                        "    |"*) echo "$line" ;;
                    esac
                done < "$LOGS/compare.log"
                [ $matched -eq 1 ] && pass "live: DP-2 under the overlay equals DP-2 before it (try $try), outside the bar" \
                    || fail "live: DP-2 under the overlay differs from DP-2 before it"
            fi
        else
            fail "live: riley-shot did not report ready within 4 s"
            excerpt "$LOGS/rs-main.out"
        fi

        # c. Escape closes everything within 2 s, exit 1 (cancelled).
        if kill -0 "$RSMAIN" 2>/dev/null; then
            wtype -k Escape 2> "$LOGS/wtype.log"
            if wait_exit "$RSMAIN" 20; then
                [ "$RC" = 1 ] && pass "live: Escape closes riley-shot within 2 s (exit 1)" \
                    || fail "live: after Escape riley-shot exits $RC, want 1 (3 means its timer, not Escape, closed it)"
            else
                fail "live: riley-shot still running 2 s after Escape (killed)"
            fi
        else
            wait_exit "$RSMAIN" 1
            fail "live: riley-shot exited (code $RC) before Escape was pressed"
            excerpt "$LOGS/rs-main.out"
        fi
        if grep -q '"exit"' "$SHOTS/report.jsonl" 2>/dev/null; then
            pass "live: report ends with an exit line"
        else
            fail "live: report has no exit line"
        fi
        sleep 0.3
        if [ -z "$(rs_layers)" ]; then
            pass "live: no riley-shot layer left"
        else
            fail "live: a riley-shot layer is still mapped: $(rs_layers | tr '\n' ';')"
        fi
    else
        fail "live: grim -o DP-2 before the overlay"
    fi
    if kill -0 "$PATTERN" 2>/dev/null; then
        kill "$PATTERN" 2>/dev/null
    fi
    wait "$PATTERN" 2>/dev/null

    # d. The timer: nothing pressed, --exit-after-ms 1500 -> exit 3 well within 4 s, layer gone.
    start_rileyshot timer --outputs DP-2 --exit-after-ms 1500 --report "$SHOTS/report-timer.jsonl" --no-editor
    if wait_exit "$RS" 40; then
        [ "$RC" = 3 ] && pass "live: --exit-after-ms 1500 closes riley-shot by itself (exit 3)" \
            || fail "live: --exit-after-ms 1500 exits $RC, want 3"
    else
        fail "live: --exit-after-ms 1500 did not close riley-shot within 4 s (killed)"
    fi
    sleep 0.3
    [ -z "$(rs_layers)" ] && pass "live: no riley-shot layer left after the timer" \
        || fail "live: a riley-shot layer is still mapped after the timer"

    # Nothing may have been captured, copied or saved in the live part.
    if [ -z "$(ls -A "$SHOTS/saved")" ] && [ ! -e "$SHOTS/clipboard/args" ]; then
        pass "live: nothing was captured, copied or saved"
    else
        fail "live: something was captured during the live checks"
    fi
    : > "$DP2_LOCK.holder"
    exec 9>&-
fi

# ---------------------------------------------------------------------------------------------------------------
if [ $FAILED -eq 0 ]; then
    echo "GATE PASS"
    exit 0
fi
echo "GATE FAIL"
exit 1
