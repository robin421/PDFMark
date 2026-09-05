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

    // Determine bounding range in rotated (u, v) space to cover the full page
    const double diag = std::hypot(static_cast<double>(widthPx), static_cast<double>(heightPx));

    int row = 0;
    for (double v = -diag; v <= diag; v += stepV, ++row) {
        // Stagger odd rows by half of u-step to interleave
        double offsetU = (row % 2 != 0) ? (0.5 * stepU) : 0.0;

        for (double u = -diag + offsetU; u <= diag; u += stepU) {
            // Transform from rotated (u, v) coordinate to page (x, y)
            double x = (widthPx / 2.0) + u * cosA - v * sinA;
            double y = (heightPx / 2.0) + u * sinA + v * cosA;

            // Expand bounding range slightly to ensure tilted text fully covers all 4 corners and borders
            if (x >= -textW && x <= widthPx + textW && y >= -textH && y <= heightPx + textH) {
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

} // namespace pdfmark