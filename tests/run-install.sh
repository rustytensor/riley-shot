#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The M4 gate: riley-shot installed on this desktop, and the M1 to M3 gates once each on the installed program.
#
#   tests/run-install.sh            build, install to ~/.local, check the installed files, run tests/run-capture.sh,
#                                   tests/run-overlay.sh and tests/run-editor.sh once each with
#                                   RILEYSHOT_BIN=<prefix>/bin/riley-shot; print PASS/FAIL per check and GATE PASS or
#                                   GATE FAIL
#   tests/run-install.sh --plant    for each tests/faults/m4-*.patch on its own: copy the tree to
#                                   /srv/devops/forge/build/riley-shot/plant-m4, apply the patch, install from there
#                                   into a scratch prefix (never ~/.local), run the gate on that install and expect it
#                                   to FAIL. The M1 to M3 gates still come from this (unpatched) tree, so only the
#                                   installed files carry the fault.
#
# Environment:
#   RILEYSHOT_PREFIX     install prefix (default $HOME/.local)
#   RILEYSHOT_BUILD_DIR  build folder for the install (default /srv/devops/forge/build/riley-shot/m4-install)
#   RILEYSHOT_GATES_ROOT the tree whose tests/run-{capture,overlay,editor}.sh run (default: this tree)
#
# What it checks (expected values fixed here, before any run):
#   1. configure and build the riley-shot program (Ninja, 8 jobs);
#   2. cmake --install --prefix PREFIX exits 0 and installs exactly these three files, no more:
#        PREFIX/bin/riley-shot                                   (executable)
#        PREFIX/share/applications/riley-shot.desktop
#        PREFIX/share/icons/hicolor/scalable/apps/riley-shot.svg
#   3. the installed program stands alone: no RPATH/RUNPATH entry into the build folder, every shared library
#      found (ldd: no "not found"); and with PREFIX/bin first on PATH, `riley-shot` resolves to PREFIX/bin/riley-shot.
#      For the default prefix the session's own PATH must find it there too (that is what the Print key runs);
#   4. the .desktop file: desktop-file-validate (when installed) reports no error or warning; the svg is well-formed
#      (xmllint); and its entries are exactly:
#        Type=Application  Name=Riley Shot  Icon=riley-shot  Terminal=false  StartupWMClass=riley-shot
#        Exec=riley-shot   Actions=region;screen;window;
#        [Desktop Action region] Exec=riley-shot --region
#        [Desktop Action screen] Exec=riley-shot --screen
#        [Desktop Action window] Exec=riley-shot --window
#      and every Exec's program resolves (PATH as in 3.) to PREFIX/bin/riley-shot. StartupWMClass is the app id the
#      program sets (QGuiApplication::setDesktopFileName("riley-shot") in src/app/main.cpp). The Exec lines are never
#      run: they would freeze every screen;
#   5. run-capture.sh, run-overlay.sh, run-editor.sh, each once, with RILEYSHOT_BIN=PREFIX/bin/riley-shot: each must
#      say "(riley-shot under test: PREFIX/bin/riley-shot)" and end in GATE PASS with exit 0. Their FAIL lines are
#      repeated here. They take the shared DP-2 lock for their own live parts; this script never holds it.
#
# Scratch files go to /srv/devops/forge/build/riley-shot/shots/m4-<pid> and are deleted on every exit.
set -u

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
FORGE=/srv/devops/forge/build/riley-shot

# ---------------------------------------------------------------------------------------------------------------
if [ "${1:-}" = "--plant" ]; then
    shopt -s nullglob
    patches=("$ROOT"/tests/faults/m4-*.patch)
    if [ ${#patches[@]} -eq 0 ]; then
        echo "PLANT: no fault patch (tests/faults/m4-*.patch); nothing to plant"
        exit 2
    fi
    PLANT=$FORGE/plant-m4
    missed=0
    for p in "${patches[@]}"; do
        rm -rf "$PLANT"
        mkdir -p "$PLANT/src"
        tar -C "$ROOT" --exclude=./.git --exclude=./build -cf - . | tar -C "$PLANT/src" -xf -
        if ! patch -d "$PLANT/src" -p1 --forward --batch --quiet < "$p"; then
            echo "PLANT: $(basename "$p") does not apply"
            rm -rf "$PLANT"
            exit 2
        fi
        echo "PLANT: applied $(basename "$p")"
        RILEYSHOT_PREFIX="$PLANT/prefix" RILEYSHOT_BUILD_DIR="$PLANT/build" RILEYSHOT_GATES_ROOT="$ROOT" \
            bash "$PLANT/src/tests/run-install.sh"
        rc=$?
        rm -rf "$PLANT"
        if [ $rc -ne 0 ]; then
            echo "PLANT CAUGHT: $(basename "$p"): the gate failed with the fault planted, as it must"
        else
            echo "PLANT MISSED: $(basename "$p"): the gate passed with the fault planted"
            missed=1
        fi
    done
    exit $missed
fi

# ---------------------------------------------------------------------------------------------------------------
PREFIX=${RILEYSHOT_PREFIX:-$HOME/.local}
BUILD=${RILEYSHOT_BUILD_DIR:-$FORGE/m4-install}
GATES=${RILEYSHOT_GATES_ROOT:-$ROOT}
LOGS=$BUILD/gate-logs
SHOTS=$FORGE/shots/m4-$$
mkdir -p "$BUILD" "$LOGS" "$SHOTS"
FAILED=0

cleanup() { rm -rf "$SHOTS"; }
trap cleanup EXIT
trap 'exit 130' INT TERM

pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; FAILED=1; }
excerpt() { tail -n 25 "$1" | sed 's/^/    | /'; }

BIN=$PREFIX/bin/riley-shot
DESKTOP=$PREFIX/share/applications/riley-shot.desktop
ICON=$PREFIX/share/icons/hicolor/scalable/apps/riley-shot.svg
echo "     (install prefix: $PREFIX)"

# --- 1. build -------------------------------------------------------------------------------------------------
built=0
if cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo > "$LOGS/configure.log" 2>&1; then
    pass "configure"
    if cmake --build "$BUILD" -j8 --target riley-shot > "$LOGS/build.log" 2>&1; then
        pass "build riley-shot"
        built=1
    else
        fail "build riley-shot (see $LOGS/build.log)"
        grep -E 'error|Error' "$LOGS/build.log" | head -n 15 | sed 's/^/    | /'
    fi
else
    fail "configure (see $LOGS/configure.log)"
    excerpt "$LOGS/configure.log"
fi

# --- 2. install -----------------------------------------------------------------------------------------------
if [ $built -eq 1 ]; then
    rm -f "$BUILD/install_manifest.txt"
    if cmake --install "$BUILD" --prefix "$PREFIX" > "$LOGS/install.log" 2>&1; then
        pass "cmake --install --prefix $PREFIX"
    else
        fail "cmake --install --prefix $PREFIX (see $LOGS/install.log)"
        excerpt "$LOGS/install.log"
    fi
    printf '%s\n' "$BIN" "$DESKTOP" "$ICON" | sort > "$SHOTS/manifest.want"
    sort "$BUILD/install_manifest.txt" > "$SHOTS/manifest.got" 2>/dev/null
    if cmp -s "$SHOTS/manifest.want" "$SHOTS/manifest.got"; then
        pass "installed exactly bin/riley-shot, the .desktop file and the icon"
    else
        fail "installed files differ from the three expected:"
        diff "$SHOTS/manifest.want" "$SHOTS/manifest.got" | sed 's/^/    | /'
    fi
fi
[ -x "$BIN" ] && pass "$BIN is executable" || fail "$BIN missing or not executable"

# --- 3. the installed program stands alone --------------------------------------------------------------------
if [ -x "$BIN" ]; then
    rpath=$(readelf -d "$BIN" 2>/dev/null | grep -E 'RPATH|RUNPATH')
    if printf '%s' "$rpath" | grep -q -e "$BUILD" -e "$FORGE"; then
        fail "installed riley-shot still looks in the build folder: $rpath"
    else
        pass "installed riley-shot has no RPATH into the build folder"
    fi
    ldd "$BIN" > "$LOGS/ldd.log" 2>&1
    if grep -q 'not found' "$LOGS/ldd.log"; then
        fail "installed riley-shot misses libraries:"
        grep 'not found' "$LOGS/ldd.log" | sed 's/^/    | /'
    else
        pass "installed riley-shot finds every shared library"
    fi
fi
resolve() { PATH="$PREFIX/bin:$PATH" command -v "$1" 2>/dev/null; }
got=$(resolve riley-shot)
[ "$got" = "$BIN" ] && pass "riley-shot on PATH (prefix first) is $BIN" \
    || fail "riley-shot on PATH (prefix first) is '${got:-nothing}', want $BIN"
if [ "$PREFIX" = "$HOME/.local" ]; then
    got=$(command -v riley-shot 2>/dev/null)
    [ "$got" = "$BIN" ] && pass "the session PATH finds riley-shot at $BIN" \
        || fail "the session PATH finds riley-shot at '${got:-nothing}', want $BIN"
fi

# --- 4. the .desktop file and the icon ------------------------------------------------------------------------
if [ -f "$DESKTOP" ]; then
    if command -v desktop-file-validate > /dev/null 2>&1; then
        desktop-file-validate "$DESKTOP" > "$LOGS/desktop-validate.log" 2>&1
        rc=$?
        if [ $rc -eq 0 ] && ! grep -q -E 'error:|warning:' "$LOGS/desktop-validate.log"; then
            pass "desktop-file-validate: no error or warning"
        else
            fail "desktop-file-validate exits $rc:"
            excerpt "$LOGS/desktop-validate.log"
        fi
    else
        echo "     (desktop-file-validate is not installed; skipped)"
    fi
    # Every key=value as "SECTION|KEY=VALUE", to compare with the expected entries.
    awk '/^\[/ { section = $0; next } /^[A-Za-z]/ { print section "|" $0 }' "$DESKTOP" > "$SHOTS/desktop.kv"
    while IFS= read -r want; do
        if grep -q -x -F -- "$want" "$SHOTS/desktop.kv"; then
            pass "desktop: $want"
        else
            fail "desktop: missing or different: $want"
        fi
    done <<'EOF'
[Desktop Entry]|Type=Application
[Desktop Entry]|Name=Riley Shot
[Desktop Entry]|Icon=riley-shot
[Desktop Entry]|Terminal=false
[Desktop Entry]|StartupWMClass=riley-shot
[Desktop Entry]|Exec=riley-shot
[Desktop Entry]|Actions=region;screen;window;
[Desktop Action region]|Exec=riley-shot --region
[Desktop Action screen]|Exec=riley-shot --screen
[Desktop Action window]|Exec=riley-shot --window
EOF
    n=$(grep -c '|Exec=' "$SHOTS/desktop.kv")
    [ "$n" = 4 ] && pass "desktop: 4 Exec lines" || fail "desktop: $n Exec lines, want 4"
    while IFS= read -r line; do
        prog=${line#*|Exec=}
        prog=${prog%% *}
        got=$(resolve "$prog")
        [ "$got" = "$BIN" ] && pass "desktop: Exec program '$prog' is $BIN" \
            || fail "desktop: Exec program '$prog' resolves to '${got:-nothing}', want $BIN"
    done < <(grep '|Exec=' "$SHOTS/desktop.kv")
else
    fail "$DESKTOP missing"
fi
if [ -f "$ICON" ] && xmllint --noout "$ICON" > "$LOGS/icon.log" 2>&1; then
    pass "icon $ICON is well-formed SVG"
else
    fail "icon $ICON missing or not well-formed"
fi

# --- 5. the M1 to M3 gates on the installed program -----------------------------------------------------------
for g in run-capture run-overlay run-editor; do
    if [ ! -x "$BIN" ]; then
        fail "$g.sh (no installed program to run it on)"
        continue
    fi
    # Each gate in its own default build folder: none of this script's settings reach it, only RILEYSHOT_BIN.
    env -u RILEYSHOT_BUILD_DIR -u RILEYSHOT_PREFIX -u RILEYSHOT_GATES_ROOT RILEYSHOT_BIN="$BIN" \
        bash "$GATES/tests/$g.sh" > "$LOGS/$g.log" 2>&1
    rc=$?
    grep -q -x -F "     (riley-shot under test: $BIN)" "$LOGS/$g.log" \
        && pass "$g.sh tested $BIN" || fail "$g.sh did not test $BIN"
    npass=$(grep -c '^PASS ' "$LOGS/$g.log")
    grep '^FAIL ' "$LOGS/$g.log" | sed "s/^FAIL /FAIL $g: /"
    if [ $rc -eq 0 ] && [ "$(tail -n 1 "$LOGS/$g.log")" = "GATE PASS" ]; then
        pass "$g.sh: GATE PASS ($npass PASS lines)"
    else
        fail "$g.sh: exit $rc, $(tail -n 1 "$LOGS/$g.log") (see $LOGS/$g.log)"
    fi
done

# ---------------------------------------------------------------------------------------------------------------
if [ $FAILED -eq 0 ]; then
    echo "GATE PASS"
    exit 0
fi
echo "GATE FAIL"
exit 1
