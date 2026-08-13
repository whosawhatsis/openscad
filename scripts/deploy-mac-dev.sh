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

echo "Checking if OpenSCAD is running..."
if osascript -e 'application "OpenSCAD" is running' | grep -q "true"; then
    echo "Error: OpenSCAD is currently running."
    echo "Please quit OpenSCAD before deploying the dev build."
    exit 1
fi

echo "Deploying dev build to $DEST_APP_PATH..."

if [ -d "$DEST_APP_PATH" ]; then
    rm -rf "$DEST_APP_PATH"
fi

cp -a "$BUILD_APP_PATH" "$DEST_APP_PATH"

echo "Deployment complete! Dev version of OpenSCAD is ready."
