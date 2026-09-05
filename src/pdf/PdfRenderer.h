// PDFMark - Page rasterization to QImage using PDFium.
#pragma once

#include "../common/Common.h"
#include "PdfDocument.h"
#include <QImage>

namespace pdfmark {

// Renders a single PDF page to a QImage at the specified DPI.
//
// The QImage is in 24-bit RGB format suitable for QPainter operations
// (watermark drawing) and JPEG compression.
//
// Output image covers the full page including rotation/proportional scaling.
class PdfRenderer {
public:
    // Rasterize page to a QImage (RGB888).
    // dpi must be in [72, 600].
    // Returns a non-null QImage. Throws PdfError on failure.
    static QImage rasterizePage(FPDF_PAGE page, int dpi);

private:
    PdfRenderer() = default;
};

} // namespace pdfmark