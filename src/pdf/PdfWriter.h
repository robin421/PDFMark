// PDFMark - Creates PDF pages containing rasterized full-page images.
#pragma once

#include "common/Common.h"
#include "pdf/PdfDocument.h"
#include <QImage>

namespace pdfmark {

class PdfWriter {
public:
    // Adds a new page to doc with dimensions (widthPt x heightPt),
    // inserts image as a full-page raster image encoded with JPEG quality,
    // and generates the content stream.
    //
    // Guaranteed O(1) memory: frees temporary buffers immediately.
    static void appendRasterPage(FPDF_DOCUMENT doc,
                                 const QImage& image,
                                 double widthPt,
                                 double heightPt,
                                 int jpegQuality = 85);

private:
    PdfWriter() = default;
};

} // namespace pdfmark