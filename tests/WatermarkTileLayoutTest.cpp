#include "watermark/WatermarkTileLayout.h"
#include <cassert>
#include <iostream>
#include <QFont>

namespace pdfmark {

void testTileLayout() {
    std::cout << "[RUN] testTileLayout\n";

    WatermarkConfig cfg;
    cfg.text = "机密文件 请勿外传";
    cfg.fontSizePt = 24;
    cfg.dpi = 200;
    cfg.rotationDegrees = -35.0;

    QFont font("Arial");
    int pxSize = WatermarkTileLayout::calculatePixelFontSize(cfg.fontSizePt, cfg.dpi);
    font.setPixelSize(pxSize);

    // Test A4 Dimensions at 200 DPI: 1654 x 2339 px
    int w = 1654;
    int h = 2339;

    auto tiles = WatermarkTileLayout::calculateLayout(w, h, cfg, font);

    // Must generate multiple tiles to cover full page
    assert(!tiles.empty());
    assert(tiles.size() >= 10);

    // Verify boundary coverage: minimum and maximum tile coordinates must exceed boundaries
    double minX = 1e9, maxX = -1e9;
    double minY = 1e9, maxY = -1e9;

    for (const auto& t : tiles) {
        if (t.center.x() < minX) minX = t.center.x();
        if (t.center.x() > maxX) maxX = t.center.x();
        if (t.center.y() < minY) minY = t.center.y();
        if (t.center.y() > maxY) maxY = t.center.y();
        assert(t.rotationDeg == -35.0);
    }

    // Assert that tiles extend beyond (0, 0) and (w, h)
    assert(minX < 0.0);
    assert(minY < 0.0);
    assert(maxX > w);
    assert(maxY > h);

    std::cout << "  Tiles generated for A4 (200 DPI): " << tiles.size()
              << " [X: " << minX << " -> " << maxX << ", Y: " << minY << " -> " << maxY << "]\n";

    std::cout << "[PASS] testTileLayout\n";
}

} // namespace pdfmark