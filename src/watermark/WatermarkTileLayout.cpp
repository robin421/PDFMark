// PDFMark - WatermarkTileLayout implementation.
#include "watermark/WatermarkTileLayout.h"
#include <QFontMetricsF>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace pdfmark {

namespace {
// Fraction of half a tile by which the outermost row/column is pulled inside the
// page. The tile therefore CROSSES the border instead of merely touching it, so
// the border strip stays covered even when the glyph's ink is inset from its
// metric box (side bearing / antialiasing): with 0.5 (tiles centred on the
// border) 38 px of a 300 DPI page still came out blank on one side.
constexpr double kBorderInsetFactor = 0.25;

// The page's bounding box once it is expressed in the watermark frame: `u` runs
// along the text direction, `v` across it.
struct RotatedBounds {
    double uMin = 0.0;
    double uMax = 0.0;
    double vMin = 0.0;
    double vMax = 0.0;
};

RotatedBounds rotatedBounds(int widthPx, int heightPx, double rotationDegrees) {
    const double rad = rotationDegrees * (std::numbers::pi / 180.0);
    const double cosA = std::cos(rad);
    const double sinA = std::sin(rad);
    const double halfW = widthPx / 2.0;
    const double halfH = heightPx / 2.0;

    RotatedBounds b{1e18, -1e18, 1e18, -1e18};
    const double cornerX[4] = {0.0, static_cast<double>(widthPx), 0.0, static_cast<double>(widthPx)};
    const double cornerY[4] = {0.0, 0.0, static_cast<double>(heightPx), static_cast<double>(heightPx)};
    for (int i = 0; i < 4; ++i) {
        const double dx = cornerX[i] - halfW;
        const double dy = cornerY[i] - halfH;
        // Inverse rotation: page (x, y) -> grid (u, v)
        const double u = dx * cosA + dy * sinA;
        const double v = -dx * sinA + dy * cosA;
        b.uMin = std::min(b.uMin, u);
        b.uMax = std::max(b.uMax, u);
        b.vMin = std::min(b.vMin, v);
        b.vMax = std::max(b.vMax, v);
    }
    return b;
}

// Tile centres along one rotated axis.
struct AxisLayout {
    double first = 0.0;    // centre of the first tile
    double step = 0.0;     // centre-to-centre spacing; 0 when count == 1
    int count = 1;         // number of tiles along this axis
    bool staggered = true; // may odd rows be shifted by half a step?
};

// Lays out the tile centres on one axis.
//
// Invariants, for every input — this is the "watermarks never overlap" contract
// of the whole file:
//   * count >= 1 and step >= extent, i.e. neighbouring centres are at least one
//     tile apart, so two watermarks are never painted on top of each other;
//   * the outermost tiles still cross both page borders, so no border strip is
//     left blank (the grid is anchored `inset` inside the border so the tile
//     crosses it by extent/2 - inset);
//   * the nominal spacing (`step`) is used whenever it does not break the two
//     rules above, so ordinary short texts keep their historical layout exactly.
//
// Why this is not just round(range / step): round() can round UP, and the old
// code then forced at least two tiles, which produced a spacing BELOW one tile
// width. Measured on the rendered page (A4 @200 DPI, -35 deg): 13-char and
// 19..21-char texts overlapped, and from ~33 chars on (text longer than the
// page) the two border tiles sat almost on top of each other on every row.
AxisLayout layoutAxis(double lo, double hi, double extent, double step, double inset) {
    const double pageExtent = hi - lo;
    const double centre = 0.5 * (lo + hi);
    const double range = pageExtent - 2.0 * inset; // span of the outermost centres

    if (range < extent) {
        // Two border-anchored tiles would be closer than one tile width, i.e.
        // they would overlap. Two gapless fallbacks instead:
        //  * one centred tile when a single tile already spans the page;
        //  * otherwise a touching pair centred on the page, whose union spans
        //    2 x extent >= pageExtent and therefore still covers both borders.
        AxisLayout out;
        out.staggered = false;
        if (extent >= pageExtent) {
            out.first = centre;
            out.step = 0.0;
            out.count = 1;
        } else {
            out.first = centre - 0.5 * extent;
            out.step = extent;
            out.count = 2;
        }
        return out;
    }

    // Nominal density, capped so that spacing = range / cols >= extent. The cap
    // only ever removes a tile in the cases the old code overlapped in.
    const int nominal = std::max(1, static_cast<int>(std::lround(range / step)));
    const int maxCols = static_cast<int>(std::floor(range / extent)); // >= 1 here
    const int cols = std::max(1, std::min(nominal, maxCols));

    AxisLayout out;
    out.first = lo + inset;
    out.step = range / cols;
    out.count = cols + 1;
    out.staggered = true;
    return out;
}
} // namespace

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

    // ── Grid geometry, expressed in the rotated (u, v) frame ───────────────
    // The grid is anchored on the page's own bounding box in (u, v) instead of a
    // fixed [-diagonal, diagonal] window, so its outermost tiles are tied to the
    // page borders (see kBorderInsetFactor and layoutAxis).
    //
    // Why: the row step is 3 x textH, so the deliberate gap between two rows can
    // land right on a page edge and leave a blank band along it. With the old
    // diagonal-anchored window + centre-based filter that happened for every
    // axis-aligned watermark (measured ink gaps: 63 px at 200 DPI with
    // rotation 0, 181 px with rotation -90), which contradicts the "edge
    // padding prevents white margins" promise.
    const RotatedBounds bounds = rotatedBounds(widthPx, heightPx, config.rotationDegrees);
    const double halfW = widthPx / 2.0;
    const double halfH = heightPx / 2.0;

    // Half extents of a tile's bounding box in page coordinates: a tile whose
    // centre is up to this far outside the page can still overlap it. Using the
    // rotated box (instead of the unrotated textW/textH slack the old filter
    // used) keeps exactly the tiles that can reach the page.
    const double halfU = textW / 2.0;
    const double halfV = textH / 2.0;
    const double boxU = halfU * std::fabs(cosA) + halfV * std::fabs(sinA);
    const double boxV = halfU * std::fabs(sinA) + halfV * std::fabs(cosA);

    const AxisLayout uAxis = layoutAxis(bounds.uMin, bounds.uMax, textW, stepU,
                                        halfU * kBorderInsetFactor);
    const AxisLayout vAxis = layoutAxis(bounds.vMin, bounds.vMax, textH, stepV,
                                        halfV * kBorderInsetFactor);

    for (int row = 0; row < vAxis.count; ++row) {
        const double v = vAxis.first + row * vAxis.step;

        // Stagger odd rows by half a u-step so the rows interleave. The stagger
        // is a pure shift of the whole row, so the near border stays covered by
        // every row; the far one is covered by the rows that are not shifted.
        // A row that holds a single centred tile (or the touching fallback pair)
        // is left alone: shifting it would pull its far end off the border.
        const double offsetU =
            (uAxis.staggered && (row % 2 != 0)) ? (-0.5 * uAxis.step) : 0.0;

        for (int col = 0; col < uAxis.count; ++col) {
            const double u = uAxis.first + col * uAxis.step + offsetU;

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
        }
    }

    return tiles;
}

double WatermarkTileLayout::rotatedExtentU(int widthPx, int heightPx, double rotationDeg) {
    if (widthPx <= 0 || heightPx <= 0) {
        return 0.0;
    }
    const RotatedBounds b = rotatedBounds(widthPx, heightPx, rotationDeg);
    return b.uMax - b.uMin;
}

} // namespace pdfmark