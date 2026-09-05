#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
DIST_DIR="${ROOT_DIR}/dist"

echo "=== PDFMark macOS Package Pipeline (APFS Staging) ==="

# 1. Build
echo "[1/6] Building Release App Bundle..."
cmake --build "${BUILD_DIR}" --config Release -j8

APP_PATH="${BUILD_DIR}/PdfMark.app"
if [ ! -d "${APP_PATH}" ]; then
    echo "Error: ${APP_PATH} was not found!"
    exit 1
fi

# 2. Setup Staging in APFS temporary directory
TMP_STAGING=$(mktemp -d /tmp/pdfmark_bundle.XXXXXX)
echo "[2/6] Staging Application Bundle in native APFS (${TMP_STAGING})..."

# Clean copy without ExFAT AppleDouble metadata
rsync -a --exclude="._*" --exclude=".DS_Store" "${APP_PATH}/" "${TMP_STAGING}/PDFMark.app/"
mkdir -p "${TMP_STAGING}/PDFMark.app/Contents/Frameworks"

PDFIUM_DYLIB="${BUILD_DIR}/libpdfium.dylib"
if [ -f "${PDFIUM_DYLIB}" ]; then
    cp "${PDFIUM_DYLIB}" "${TMP_STAGING}/PDFMark.app/Contents/Frameworks/libpdfium.dylib"
    chmod +w "${TMP_STAGING}/PDFMark.app/Contents/Frameworks/libpdfium.dylib"
    chmod +w "${TMP_STAGING}/PDFMark.app/Contents/MacOS/PdfMark"
    install_name_tool -id "@rpath/libpdfium.dylib" "${TMP_STAGING}/PDFMark.app/Contents/Frameworks/libpdfium.dylib" 2>/dev/null || true
    install_name_tool -change "./libpdfium.dylib" "@rpath/libpdfium.dylib" "${TMP_STAGING}/PDFMark.app/Contents/MacOS/PdfMark" 2>/dev/null || true
fi

# 3. Run macdeployqt on APFS
echo "[3/6] Deploying Qt frameworks and plugins..."
MACDEPLOYQT_BIN="$(which macdeployqt || echo '/opt/homebrew/opt/qtbase/bin/macdeployqt')"
if [ ! -x "${MACDEPLOYQT_BIN}" ]; then
    MACDEPLOYQT_BIN="$(brew --prefix qt 2>/dev/null)/bin/macdeployqt" || true
fi

"${MACDEPLOYQT_BIN}" "${TMP_STAGING}/PDFMark.app" -always-overwrite -no-strip 2>&1 || true

# 4. Clean up any residual ._* files and codesign
echo "[4/6] Code signing bundle..."
find "${TMP_STAGING}/PDFMark.app" -name "._*" -delete 2>/dev/null || true
codesign --force --deep --sign - "${TMP_STAGING}/PDFMark.app"
codesign -v --verbose=4 "${TMP_STAGING}/PDFMark.app"

# 5. Create DMG & ZIP directly on APFS for maximum speed and integrity
echo "[5/6] Creating distribution archives..."
mkdir -p "${DIST_DIR}"
rm -f "${DIST_DIR}/PDFMark-macOS.zip" "${DIST_DIR}/PDFMark-macOS.dmg"

# Create zip
(cd "${TMP_STAGING}" && zip -r -q "${DIST_DIR}/PDFMark-macOS.zip" "PDFMark.app")

# Create dmg
hdiutil create -volname "PDFMark" -srcfolder "${TMP_STAGING}/PDFMark.app" -ov -format UDZO "${DIST_DIR}/PDFMark-macOS.dmg"

# Also sync the clean PDFMark.app back to dist for direct execution
rm -rf "${DIST_DIR}/PDFMark.app"
rsync -a "${TMP_STAGING}/PDFMark.app/" "${DIST_DIR}/PDFMark.app/"

# Cleanup temporary APFS staging directory
rm -rf "${TMP_STAGING}"

echo ""
echo "========================================="
echo " macOS Packaging Succeeded!"
echo " DMG: ${DIST_DIR}/PDFMark-macOS.dmg"
echo " ZIP: ${DIST_DIR}/PDFMark-macOS.zip"
echo " APP: ${DIST_DIR}/PDFMark.app"
echo "========================================="
