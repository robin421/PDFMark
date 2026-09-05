// PDFMark - Watermark rendering engine onto QImage.
#pragma once

#include "watermark/WatermarkConfig.h"
#include <QImage>

namespace pdfmark {

class WatermarkRenderer {
public:
    // Paints repeating tiled watermark directly onto the provided QImage.
    // The watermark becomes permanently fused with the image pixels.
    //
    // Returns true on success, false if configuration is invalid or image is null.
    static bool applyWatermark(QImage& image, const WatermarkConfig& config);

    // Generates a preview image of specified dimensions (e.g. A4 aspect ratio)
    // with a simulated white document background and the configured watermark.
    static QImage renderPreview(int widthPx, int heightPx, const WatermarkConfig& config);

private:
    WatermarkRenderer() = default;
};

} // namespace pdfmark