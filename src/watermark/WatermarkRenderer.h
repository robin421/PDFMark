// PDFMark - Watermark rendering engine onto QImage.
#pragma once

#include "watermark/WatermarkConfig.h"
#include "watermark/WatermarkTileLayout.h"
#include <vector>
#include <QImage>
#include <QFont>

namespace pdfmark {

class WatermarkRenderer {
public:
    // Pre-rendered, pre-rotated watermark tile stamp.
    // Caches text rasterization, font layout, and rotation transform so multiple
    // pages and dozens of tiles can be blended via ultra-fast bitblt operations.
    struct Stamp {
        QImage image;
        QFont font;
        double rotationDeg = 0.0;
        [[nodiscard]] bool isValid() const { return !image.isNull(); }
    };

    // Pre-renders a single tile stamp for the specified configuration.
    static Stamp createStamp(const WatermarkConfig& config);

    // Paints repeating tiled watermark directly onto the provided QImage.
    // Generates a temporary Stamp internally.
    static bool applyWatermark(QImage& image, const WatermarkConfig& config);

    // Paints repeating tiled watermark using an already-created Stamp.
    static bool applyWatermark(QImage& image, const WatermarkConfig& config, const Stamp& stamp);

    // Fill a caller-owned buffer from a freshly rasterized page. The buffer is
    // (re)allocated only when its size/format changes, so reusing one scratch
    // buffer across pages avoids a full-page heap allocation per page.
    static bool fillBuffer(QImage& scratch, const QImage& source);

    // Blit an already-rendered, pre-rotated stamp at every tile position.
    // Tiles only depend on (image size, config, font), so callers may compute
    // them once and reuse them across every page.
    static void blitTiles(QImage& target, const Stamp& stamp,
                          const std::vector<TileItem>& tiles);

    // Generates a preview image of specified dimensions (e.g. A4 aspect ratio)
    // with a simulated white document background and the configured watermark.
    static QImage renderPreview(int widthPx, int heightPx, const WatermarkConfig& config);

    // Same, but overlays every line of a template (all sharing one style).
    static QImage renderPreview(int widthPx, int heightPx,
                                const std::vector<WatermarkConfig>& configs);

private:
    WatermarkRenderer() = default;
};

} // namespace pdfmark