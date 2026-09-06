// PDFMark - Watermark rendering engine onto QImage.
#pragma once

#include "watermark/WatermarkConfig.h"
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

    // Generates a preview image of specified dimensions (e.g. A4 aspect ratio)
    // with a simulated white document background and the configured watermark.
    static QImage renderPreview(int widthPx, int heightPx, const WatermarkConfig& config);

private:
    WatermarkRenderer() = default;
};

} // namespace pdfmark