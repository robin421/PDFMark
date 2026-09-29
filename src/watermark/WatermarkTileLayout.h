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
    // Two guarantees, both tested in tests/WatermarkTileLayoutTest.cpp:
    //  * NO OVERLAP: two tiles are never drawn on top of each other, for any
    //    text length, font size, page size or rotation. A text longer than the
    //    page degrades to a single centred (or touching) tile per row instead of
    //    two border tiles stacking onto each other.
    //  * NO BLANK BAND: the grid is anchored on the page's bounding box in the
    //    rotated frame, so the outermost tiles cross every page border and the
    //    deliberate gaps between tiles never fall on a page edge.
    static std::vector<TileItem> calculateLayout(int widthPx,
                                                 int heightPx,
                                                 const WatermarkConfig& config,
                                                 const QFont& font);

    // Helper: computes effective pixel font size from points and DPI.
    static int calculatePixelFontSize(int fontSizePt, int dpi);

    // Extent of the page along the WATERMARK TEXT direction once the page's
    // bounding box is expressed in the rotated frame, i.e.
    // widthPx*|cos| + heightPx*|sin|.
    //
    // Single source of truth for "how much text a single tile can ever show on
    // this page": a tile centre lies inside the page's rotated bounding box, so
    // only the text inside this window can reach the page. WatermarkRenderer::
    // createStamp() crops a very long stamp to this width, which keeps the
    // rotated raster bounded (see createStamp for the numbers).
    static double rotatedExtentU(int widthPx, int heightPx, double rotationDeg);
};

} // namespace pdfmark