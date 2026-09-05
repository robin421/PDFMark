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
    // Calculates the list of tile items that completely covers the given target
    // rectangle [0, widthPx] x [0, heightPx] with staggered diagonal repeating text.
    //
    // Guaranteed to leave no corners unwatermarked by padding bounding tiles.
    static std::vector<TileItem> calculateLayout(int widthPx,
                                                 int heightPx,
                                                 const WatermarkConfig& config,
                                                 const QFont& font);

    // Helper: computes effective pixel font size from points and DPI.
    static int calculatePixelFontSize(int fontSizePt, int dpi);
};

} // namespace pdfmark