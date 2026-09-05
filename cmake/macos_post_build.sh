#!/bin/bash
set -e
BUNDLE_DIR="$1"
EXEC_FILE="$2"
DYLIB_FILE="$3"

if [ -f "$DYLIB_FILE" ]; then
    install_name_tool -id "@loader_path/libpdfium.dylib" "$DYLIB_FILE"
fi

if [ -f "$EXEC_FILE" ] && [ -f "$DYLIB_FILE" ]; then
    install_name_tool -change "./libpdfium.dylib" "@loader_path/libpdfium.dylib" "$EXEC_FILE"
fi

if [ -d "$BUNDLE_DIR" ]; then
    # Clean extended attributes / AppleDouble files that break code signing
    find "$BUNDLE_DIR" -name "._*" -delete 2>/dev/null || true
    # Sign app bundle ad-hoc
    codesign -s - --force --deep "$BUNDLE_DIR"
fi
