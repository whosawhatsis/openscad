#!/usr/bin/env bash
# Run the GUI test suite without leaving the developer's own OpenSCAD settings modified.
#
# --run-all-gui-tests drives a real MainWindow, so every QSettings write it makes lands
# in the same preferences domain the installed app reads: view mode, window geometry,
# recent files, and menu toggles such as Design > Automatic Reload and Compile. Running
# the suite therefore silently reconfigures the app you use.
#
# This snapshots the whole domain, runs the tests, and restores it afterwards - including
# when the tests crash, fail, or the run is interrupted.
#
# Usage: scripts/run-gui-tests.sh [path-to-OpenSCAD-binary] [extra args...]

set -u

DOMAIN="org.openscad.OpenSCAD"
BINARY="${1:-build-gui/OpenSCAD.app/Contents/MacOS/OpenSCAD}"
[ $# -gt 0 ] && shift

if [ ! -x "$BINARY" ]; then
    echo "Error: no GUI test binary at $BINARY" >&2
    echo "Build one with: cmake -B build-gui -DEXPERIMENTAL=1 -DSNAPSHOT=1 -DENABLE_TESTS=1 -DENABLE_GUI_TESTS=1" >&2
    echo "(ENABLE_GUI_TESTS cannot be turned on in an existing build directory - it needs its own.)" >&2
    exit 1
fi

if [ "$(uname)" != "Darwin" ]; then
    # Elsewhere QSettings writes an INI file; copy it aside rather than using `defaults`.
    CONF="${XDG_CONFIG_HOME:-$HOME/.config}/OpenSCAD/OpenSCAD.conf"
    SNAPSHOT="$(mktemp)"
    [ -f "$CONF" ] && cp "$CONF" "$SNAPSHOT"
    restore() {
        if [ -s "$SNAPSHOT" ]; then mkdir -p "$(dirname "$CONF")"; cp "$SNAPSHOT" "$CONF"; else rm -f "$CONF"; fi
        rm -f "$SNAPSHOT"
    }
else
    SNAPSHOT="$(mktemp -t openscad-prefs).plist"
    # `defaults export` rather than copying the plist: macOS serves preferences through
    # cfprefsd, which caches them and will happily overwrite a file swapped underneath it.
    defaults export "$DOMAIN" "$SNAPSHOT" 2>/dev/null || echo "{}" > "$SNAPSHOT"
    restore() {
        defaults import "$DOMAIN" "$SNAPSHOT" 2>/dev/null
        rm -f "$SNAPSHOT"
    }
fi

trap 'restore' EXIT INT TERM

"$BINARY" --run-all-gui-tests "$@"
status=$?

# The tests fake a crash-free exit even when a QTest case fails, so report both the
# process status and, for the caller's benefit, that settings were put back.
echo "--- GUI tests exited with status $status; $DOMAIN restored from snapshot ---"
exit $status
