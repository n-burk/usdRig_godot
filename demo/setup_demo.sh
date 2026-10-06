#!/usr/bin/env bash
# Sets up the demo project: copies the addon in (never committed) and
# bakes the sample character with the dev machine's rigExecBake, once at
# 1001 (fk.rigexec) and once at 1002 (fk_1002.rigexec, whose input
# defaults verify.gd drives fk.rigexec with).
# Usage: setup_demo.sh [path/to/usdRig]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
USDRIG="${1:-$HERE/../../usdRig}"
if [ ! -x "$USDRIG/build/rigExecBake" ]; then
    echo "FATAL: no rigExecBake at $USDRIG/build; build usdRig first" >&2
    exit 2
fi
if [ ! -f "$HERE/../addons/rigexec/bin/librigexec.linux.template_debug.x86_64.so" ]; then
    echo "FATAL: no extension library; run scons in godot_rigExec first" >&2
    exit 2
fi
rm -rf "$HERE/addons"
mkdir -p "$HERE/addons"
# Same exclusion as setup_exclude.txt, plus the Windows/macOS libraries:
# the demo carries only what its platform loads.
tar cf - --exclude='*.obj' --exclude='*.os' --exclude='*.exp' \
    --exclude='*.lib' --exclude='*.pdb' --exclude='.sconsign.dblite' \
    --exclude='src' \
    --exclude='*windows*' --exclude='*macos*' --exclude='*.framework' \
    -C "$HERE/.." addons | tar xf - -C "$HERE"
USDINSTALL="$HERE/../../usd-install"
export LD_LIBRARY_PATH="$USDINSTALL/lib:${LD_LIBRARY_PATH:-}"
export PXR_PLUGINPATH_NAME="$USDRIG/build/usd/rigExecSchema/resources"
"$USDRIG/build/rigExecBake" "$USDRIG/examples/01_FkChainTail.usda" \
    --time 1001 -o "$HERE/fk.rigexec"
"$USDRIG/build/rigExecBake" "$USDRIG/examples/01_FkChainTail.usda" \
    --time 1002 -o "$HERE/fk_1002.rigexec"
# Cached imports of earlier bytes would outlive the rebake.
rm -rf "$HERE/.godot"
if command -v godot >/dev/null 2>&1; then
    # Known upstream quirk (not this addon's bug): the FIRST --import on
    # a clean tree can abort with exit 134 during editor teardown AFTER
    # the import succeeded -- a second run exits 0. It reproduces with
    # an empty one-class extension (no rigExec code on the abort's
    # stack), and the game/runtime path always exits 0. So the gate
    # here is the import PRODUCT, not the exit code: a nonzero exit
    # with the .import receipt present is the quirk, warned about;
    # a missing receipt is a real failure.
    # Dropped first so the receipts below prove THIS run imported --
    # a stale one would otherwise mask a failed re-import.
    rm -f "$HERE/fk.rigexec.import" "$HERE/fk_1002.rigexec.import"
    set +e
    godot --headless --path "$HERE" --import
    IMPORT_RC=$?
    set -e
    if [ ! -f "$HERE/fk.rigexec.import" ] ||
        [ ! -f "$HERE/fk_1002.rigexec.import" ]; then
        echo "FATAL: editor import failed (exit $IMPORT_RC)" >&2
        exit 1
    fi
    if [ "$IMPORT_RC" -ne 0 ]; then
        echo "WARNING: editor --import exited $IMPORT_RC after writing" \
            "the .import receipts (known first-import teardown abort;" \
            "re-run exits 0)."
    fi
else
    echo "WARNING: godot not on PATH; skipping editor import."
    echo "Run: godot --headless --path \"$HERE\" --import"
fi
echo "demo ready: fk.rigexec and fk_1002.rigexec baked, addon copied"
