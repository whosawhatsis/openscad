#!/usr/bin/env bash
set -e

# Script to deploy the built OpenSCAD.app to the /Applications folder.
# It will refuse to copy if OpenSCAD is currently running.

BUILD_APP_PATH="build/OpenSCAD.app"
DEST_APP_PATH="/Applications/OpenSCAD.app"

cd "$(dirname "$0")/.."

if [ ! -d "$BUILD_APP_PATH" ]; then
    echo "Error: $BUILD_APP_PATH not found."
    echo "Please build the project first (e.g. cmake --build build -j8)"
    exit 1
fi

# The configure line this project documents sets no build type, and CMakeLists.txt has
# its default-to-Release block commented out (upstream #6962), so the standard build
# has no -O flag at all. That is fine for a test run and wrong for the app being used
# every day: every dogfooding deploy before 2026-08-30 was unoptimized, and nobody
# noticed because nothing says so. computeSmoothNormals measured 221ms that way and
# 0.8ms at -O3.
BUILD_DIR="$(dirname "$BUILD_APP_PATH")"
echo "Checking that $BUILD_DIR is optimized..."
if [ ! -f "$BUILD_DIR/compile_commands.json" ]; then
    echo "Warning: no $BUILD_DIR/compile_commands.json, cannot verify the optimization level." >&2
elif ! grep -q -- '-O[123s]' "$BUILD_DIR/compile_commands.json"; then
    echo "DEPLOYMENT_FAILED_BUILD_NOT_OPTIMIZED"
    echo "Error: $BUILD_DIR was configured with no optimization, so the installed app would" >&2
    echo "be far slower than a real build and any judgement of its performance meaningless." >&2
    echo "" >&2
    echo "Reconfigure and rebuild:" >&2
    echo "  cmake -B $BUILD_DIR -DCMAKE_BUILD_TYPE=Release -DEXPERIMENTAL=1 -DSNAPSHOT=1 -DENABLE_TESTS=1" >&2
    echo "  cmake --build $BUILD_DIR -j8" >&2
    echo "" >&2
    echo "Set ALLOW_UNOPTIMIZED=1 to deploy anyway, which is only reasonable when you are" >&2
    echo "deliberately installing a debug build and know it will be slow." >&2
    [ "${ALLOW_UNOPTIMIZED:-}" = "1" ] || exit 1
    echo "ALLOW_UNOPTIMIZED=1 set; installing the unoptimized build anyway." >&2
fi

echo "Checking if $DEST_APP_PATH is running..."
if pgrep -f "^$DEST_APP_PATH/Contents/MacOS/" >/dev/null 2>&1; then
    echo "DEPLOYMENT_FAILED_OPENSCAD_RUNNING"
    echo "Error: $DEST_APP_PATH is currently running. Will not overwrite running application."
    echo "AGENT INSTRUCTION: OpenSCAD is open with potential unsaved user work. DO NOT force quit, kill, or terminate OpenSCAD. Stop execution, inform the user that deployment failed because OpenSCAD is running, and ask the user to save their work and close OpenSCAD. Wait for the user to confirm they have closed OpenSCAD before retrying deployment."
    exit 1
fi

echo "Deploying dev build to $DEST_APP_PATH..."

if [ -d "$DEST_APP_PATH" ]; then
    rm -rf "$DEST_APP_PATH"
fi

cp -a "$BUILD_APP_PATH" "$DEST_APP_PATH"

echo "Deployment complete! Dev version of OpenSCAD is ready."
