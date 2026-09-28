// PDFMark - WatermarkTileLayout implementation.
#include "watermark/WatermarkTileLayout.h"
#include <QFontMetricsF>
#include <cmath>
#include <numbers>

namespace pdfmark {

int WatermarkTileLayout::calculatePixelFontSize(int fontSizePt, int dpi) {
    if (fontSizePt <= 0) fontSizePt = 24;
    if (dpi <= 0) dpi = 200;
    return std::max(8, static_cast<int>(std::round(fontSizePt * dpi / 72.0)));
}

std::vector<TileItem> WatermarkTileLayout::calculateLayout(int widthPx,
                                                         int heightPx,
                                                         const WatermarkConfig& config,
                                                         const QFont& font) {
    std::vector<TileItem> tiles;
    if (widthPx <= 0 || heightPx <= 0 || config.text.empty()) {
        return tiles;
    }

    QFontMetricsF fm(font);
    QString qtext = QString::fromStdString(config.text);
    QRectF rawRect = fm.boundingRect(qtext);

    double textW = std::max(10.0, rawRect.width());
    double textH = std::max(8.0, rawRect.height());

    // Convert rotation to radians preserving exact sign so grid aligns with text tilt
    const double rad = config.rotationDegrees * (std::numbers::pi / 180.0);
    const double cosA = std::cos(rad);
    const double sinA = std::sin(rad);
    // Calculate font size to derive per-character metrics (A4 width 1654 px)
    // We calculate character width based on textW / character count for precise gaps
    const int charCount = qtext.length();
    const double avgCharW = std::max(1.0, textW / double(charCount));

    // Spec: 上下间隙 = 2 字体高度 (gapV), 左右间隙 = 2 字体宽度 (gapU)
    // Grid step is aligned on the rotated coordinate axes:
    // - Along text direction (u-axis): textW + 2 * avgCharW
    // - Perpendicular text direction (v-axis): textH + 2 * textH
    const double gapU = 2.0 * avgCharW;
    const double gapV = 2.0 * textH;
    const double stepU = textW + gapU;
    const double stepV = textH + gapV;

    // Local centered text rectangle
    QRectF localRect(-textW / 2.0, -textH / 2.0, textW, textH);

    // ── Grid extent, expressed in the rotated (u, v) frame ───────────────
    //
    // The grid is anchored on the page's own bounding box in (u, v) instead of a
    // fixed [-diagonal, diagonal] window, and its last row/column is clamped
    // exactly onto the far border.
    //
    // Why: the row step is 3 x textH, so the deliberate gap between two rows can
    // land right on a page edge and leave a blank band along it. With the old
    // diagonal-anchored window + centre-based filter that happened for every
    // axis-aligned watermark (measured ink gaps: 63 px at 200 DPI with
    // rotation 0, 181 px with rotation -90), which contradicts the "edge
    // padding prevents white margins" promise.
    const double halfW = widthPx / 2.0;
    const double halfH = heightPx / 2.0;
    double uMin = 1e18, uMax = -1e18, vMin = 1e18, vMax = -1e18;
    const double cornerX[4] = {0.0, static_cast<double>(widthPx), 0.0, static_cast<double>(widthPx)};
    const double cornerY[4] = {0.0, 0.0, static_cast<double>(heightPx), static_cast<double>(heightPx)};
    for (int i = 0; i < 4; ++i) {
        const double dx = cornerX[i] - halfW;
        const double dy = cornerY[i] - halfH;
        // Inverse rotation: page (x, y) -> grid (u, v)
        const double u = dx * cosA + dy * sinA;
        const double v = -dx * sinA + dy * cosA;
        uMin = std::min(uMin, u);
        uMax = std::max(uMax, u);
        vMin = std::min(vMin, v);
        vMax = std::max(vMax, v);
    }

    // Half extents of a tile's bounding box in page coordinates: a tile whose
    // centre is up to this far outside the page can still overlap it. Using the
    // rotated box (instead of the unrotated textW/textH slack the old filter
    // used) keeps exactly the tiles that can reach the page.
    const double halfU = textW / 2.0;
    const double halfV = textH / 2.0;
    const double boxU = halfU * std::fabs(cosA) + halfV * std::fabs(sinA);
    const double boxV = halfU * std::fabs(sinA) + halfV * std::fabs(cosA);

    int row = 0;
    for (double v = vMin;; v += stepV, ++row) {
        if (v > vMax) v = vMax;          // clamp the last row onto the far border
        const bool lastRow = (v >= vMax);

        // Stagger odd rows by half of u-step to interleave. The stagger only
        // EXTENDS the row to the right, so the first column always stays on the
        // near border and a staggered row can never open a gap at the page edge.
        const double offsetU = (row % 2 != 0) ? (0.5 * stepU) : 0.0;
        const double uEnd = uMax + offsetU;

        for (double u = uMin;; u += stepU) {
            if (u + stepU > uEnd) u = std::max(u, uEnd);   // clamp onto the far border
            const bool lastCol = (u >= uEnd);

            // Transform from rotated (u, v) coordinate to page (x, y)
            const double x = halfW + u * cosA - v * sinA;
            const double y = halfH + u * sinA + v * cosA;

            // Keep every tile whose (rotated) box can reach the page.
            if (x + boxU >= 0.0 && x - boxU <= widthPx &&
                y + boxV >= 0.0 && y - boxV <= heightPx) {
                TileItem item;
                item.center = QPointF(x, y);
                item.rotationDeg = config.rotationDegrees;
                item.textBounds = localRect;
                tiles.push_back(item);
            }
            if (lastCol) break;
        }
        if (lastRow) break;
    }

    return tiles;
}

} // namespace pdfmark