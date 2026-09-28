// PDFMark - Watermark tile layout geometry and calculations.
#pragma once

#include "watermark/WatermarkConfig.h"
#include <vector>
#include <QPointF>
#include <QRectF>
#include <QFont>
#include <QString>

namespace pdfmark {

struct TileItem {
    QPointF center;      // Center position of this tile in target image coordinates
    double rotationDeg;  // Rotation angle in degrees
    QRectF textBounds;   // Unrotated local text bounding box centered at (0, 0)
};

class WatermarkTileLayout {
public:
    // Calculates the list of tile items that covers the given target rectangle
    // [0, widthPx] x [0, heightPx] with staggered diagonal repeating text.
    //
    // Guarantees that no page border is left with a blank band: the grid is
    // anchored on the page's bounding box in the rotated frame and its last
    // row/column is clamped onto the far border, so the deliberate gaps between
    // tiles can never fall on a page edge.
    static std::vector<TileItem> calculateLayout(int widthPx,
                                                 int heightPx,
                                                 const WatermarkConfig& config,
                                                 const QFont& font);

    // Helper: computes effective pixel font size from points and DPI.
    static int calculatePixelFontSize(int fontSizePt, int dpi);
};

} // namespace pdfmark