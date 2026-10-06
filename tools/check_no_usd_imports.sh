#!/bin/sh
# Import check (POSIX): the shipped runtime library must not import USD.
# Usage: check_no_usd_imports.sh <librigexec....so|.dylib>
# Exits 1 naming the first USD import found.
set -u
DLL=${1:-}
if [ -z "$DLL" ]; then
    echo "usage: check_no_usd_imports.sh <lib>" >&2
    exit 2
fi
LEAKS=$( (ldd "$DLL" 2>/dev/null || otool -L "$DLL" 2>/dev/null) \
    | grep -i -e usd_ -e pxr -e libtbb || true )
if [ -n "$LEAKS" ]; then
    echo "$LEAKS" | sed 's/^/USD-LEAK: /'
    echo "FAIL: $DLL links USD libraries"
    exit 1
fi
echo "OK: $DLL links no USD libraries"
