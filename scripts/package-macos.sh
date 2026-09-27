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

# Locate libpdfium.dylib. The app bundle produced by CMake already ships it in
# Contents/MacOS next to the executable (referenced as @loader_path), which is
# what actually matters; we additionally mirror it into Frameworks and rewrite
# the reference so the layout stays consistent.
PDFIUM_DYLIB=""
for cand in \
    "${APP_PATH}/Contents/MacOS/libpdfium.dylib" \
    "${BUILD_DIR}/libpdfium.dylib" \
    "${ROOT_DIR}/third_party/pdfium/lib/libpdfium.dylib"; do
    if [ -f "${cand}" ]; then
        PDFIUM_DYLIB="${cand}"
        break
    fi
done

if [ -z "${PDFIUM_DYLIB}" ]; then
    echo "Error: libpdfium.dylib not found (looked in the app bundle, ${BUILD_DIR} and third_party/pdfium/lib)."
    exit 1
fi

echo "    using pdfium: ${PDFIUM_DYLIB}"
cp "${PDFIUM_DYLIB}" "${TMP_STAGING}/PDFMark.app/Contents/Frameworks/libpdfium.dylib"
chmod +w "${TMP_STAGING}/PDFMark.app/Contents/Frameworks/libpdfium.dylib"
chmod +w "${TMP_STAGING}/PDFMark.app/Contents/MacOS/PdfMark"
install_name_tool -id "@rpath/libpdfium.dylib" \
    "${TMP_STAGING}/PDFMark.app/Contents/Frameworks/libpdfium.dylib" 2>/dev/null || true
# Normalise whatever reference the build produced to the Frameworks copy.
for ref in "./libpdfium.dylib" "@loader_path/libpdfium.dylib" "@executable_path/libpdfium.dylib"; do
    install_name_tool -change "${ref}" "@rpath/libpdfium.dylib" \
        "${TMP_STAGING}/PDFMark.app/Contents/MacOS/PdfMark" 2>/dev/null || true
done

# 3. Run macdeployqt on APFS.
# IMPORTANT: macdeployqt MUST come from the same Qt the binary was built against.
# Using whatever `which macdeployqt` happens to find (e.g. a conda Qt) silently
# produces a bundle with missing frameworks (observed: QtDBus.framework absent),
# which crashes on launch with "Library not loaded: @rpath/QtDBus.framework/...".
MACDEPLOYQT_BIN=""

# Preferred: ask qmake6 of the Qt actually used for the build.
QT_BINS="$(qmake6 -query QT_INSTALL_BINS 2>/dev/null || true)"
if [ -n "${QT_BINS}" ] && [ -x "${QT_BINS}/macdeployqt" ]; then
    MACDEPLOYQT_BIN="${QT_BINS}/macdeployqt"
fi

# Fallback: Homebrew Qt.
if [ -z "${MACDEPLOYQT_BIN}" ]; then
    BREW_QT="$(brew --prefix qt 2>/dev/null || true)"
    if [ -n "${BREW_QT}" ] && [ -x "${BREW_QT}/bin/macdeployqt" ]; then
        MACDEPLOYQT_BIN="${BREW_QT}/bin/macdeployqt"
    fi
fi

if [ -z "${MACDEPLOYQT_BIN}" ]; then
    echo "Error: macdeployqt not found. Install Qt 6 (brew install qt) or put its bin dir on PATH."
    exit 1
fi

echo "    using macdeployqt: ${MACDEPLOYQT_BIN}"
"${MACDEPLOYQT_BIN}" "${TMP_STAGING}/PDFMark.app" -always-overwrite -no-strip

# 4. Clean up any residual ._* files and codesign
echo "[4/6] Code signing bundle..."
find "${TMP_STAGING}/PDFMark.app" -name "._*" -delete 2>/dev/null || true
codesign --force --deep --sign - "${TMP_STAGING}/PDFMark.app"
codesign -v --verbose=4 "${TMP_STAGING}/PDFMark.app"

# 4b. Verify the staged bundle is self-contained BEFORE shipping it.
# Walks every Mach-O file and fails if an @rpath/@loader_path/@executable_path
# dependency cannot be resolved inside the bundle.
verify_bundle() {
    local bundle="$1"
    local root="${bundle}/Contents"
    local missing=0
    while IFS= read -r bin; do
        [ -z "${bin}" ] && continue
        file "${bin}" 2>/dev/null | grep -q "Mach-O" || continue
        while IFS= read -r dep; do
            case "${dep}" in
                @rpath/*|@loader_path/*|@executable_path/*) ;;
                *) continue ;;
            esac
            local base="${dep##*/}"
            if [ ! -e "${root}/MacOS/${base}" ] && \
               [ ! -e "${root}/Frameworks/${base}" ] && \
               [ -z "$(find "${root}" -name "${base}" -print -quit 2>/dev/null)" ]; then
                echo "    MISSING: ${base}  <- required by ${bin#${root}/}"
                missing=1
            fi
        done < <(otool -L "${bin}" 2>/dev/null | tail -n +2 | awk '{print $1}')
    done < <(find "${root}" -type f -perm -u+x -o -type f -name "*.dylib" 2>/dev/null)

    return ${missing}
}

echo "[5/7] Verifying bundle dependencies..."
if ! verify_bundle "${TMP_STAGING}/PDFMark.app"; then
    echo "Error: bundle is not self-contained (see MISSING entries above)."
    exit 1
fi
echo "    all dynamic dependencies resolved inside the bundle"

# Smoke-test the packaged app so a broken bundle never gets shipped.
# macdeployqt only ships the platform plugins the app actually needs, so on
# macOS the deployed bundle contains "cocoa" (not "offscreen"). Launch exactly
# the way a user would; skip (with a warning) when there is no GUI session.
echo "[6/7] Smoke-testing packaged app..."
if [ "$(launchctl managername 2>/dev/null)" != "Aqua" ]; then
    echo "    WARNING: no Aqua session detected - skipping launch test"
    echo "    (dynamic dependency verification above already passed)"
else
    "${TMP_STAGING}/PDFMark.app/Contents/MacOS/PdfMark" > /tmp/pdfmark_pkg_smoke.log 2>&1 &
    SMOKE_PID=$!
    sleep 5
    if kill -0 "${SMOKE_PID}" 2>/dev/null; then
        kill "${SMOKE_PID}" 2>/dev/null || true
        wait "${SMOKE_PID}" 2>/dev/null || true
        echo "    packaged app launched successfully"
    else
        echo "Error: packaged app exited immediately. Log:"
        cat /tmp/pdfmark_pkg_smoke.log || true
        exit 1
    fi
fi

# 6. Create DMG & ZIP directly on APFS for maximum speed and integrity
echo "[7/7] Creating distribution archives..."
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
