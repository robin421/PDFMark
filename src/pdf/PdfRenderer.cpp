// PDFMark - Page rasterization to QImage using PDFium.
#define NOMINMAX  // Prevent Windows macros from polluting std::min/std::max

#include "pdf/PdfRenderer.h"
#include "pdf/PdfDocument.h"
#include "pdf/PdfLibrary.h"
#include "common/Common.h"

#include <fpdfview.h>
#include <cmath>

namespace pdfmark {

QImage PdfRenderer::rasterizePage(FPDF_PAGE page, int dpi) {
    if (!page) throw PdfError("Null page handle");

    double widthPt = 0, heightPt = 0;
    try {
        widthPt = PdfDocument::getPageWidth(page);
        heightPt = PdfDocument::getPageHeight(page);
    } catch (...) {
        throw PdfError("Failed to read page dimensions from PDF");
    }

    if (widthPt <= 0.0 || heightPt <= 0.0) {
        throw PdfError("Invalid page dimensions");
    }

    if (dpi < 10 || dpi > 1200) {
        throw PdfError("DPI out of range (10-1200): " + std::to_string(dpi));
    }

    int widthPx = (std::max)(1, static_cast<int>(std::round(widthPt * dpi / 72.0)));
    int heightPx = (std::max)(1, static_cast<int>(std::round(heightPt * dpi / 72.0)));

    // Guard against absurdly large allocations (> 100 MPix)
    if (widthPx > 10000 || heightPx > 10000) {
        throw PdfError("Page too large to rasterize safely: " +
                       std::to_string(widthPx) + "x" + std::to_string(heightPx) +
                       " pixels (limit: 10000px per dimension)");
    }

    // Create a QImage that owns its pixel memory — the buffer is pre-allocated
    // by QImage itself and reused across pages on the same thread.
    QImage image(widthPx, heightPx, QImage::Format_RGB32);
    if (image.isNull()) {
        throw PdfError("Failed to allocate QImage of size " +
                       std::to_string(widthPx) + "x" + std::to_string(heightPx));
    }

    // FPDFBitmap wraps the QImage's pixel buffer directly (external buffer mode).
    // This avoids allocating/deallocating a separate ~35 MB FPDF_BITMAP heap buffer
    // on every page, which eliminates Windows heap allocator lock contention and
    // reduces page-fault pressure significantly.
    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(widthPx, heightPx,
                                             FPDFBitmap_BGRx, // matches QImage::Format_RGB32 on little-endian
                                             image.bits(),
                                             image.bytesPerLine());
    if (!bitmap) {
        throw PdfError("FPDFBitmap_CreateEx failed for " +
                       std::to_string(widthPx) + "x" + std::to_string(heightPx));
    }

    try {
        FPDFBitmap_FillRect(bitmap, 0, 0, widthPx, heightPx, 0xFFFFFFFF);
        FPDF_RenderPageBitmap(bitmap, page, 0, 0, widthPx, heightPx, 0, FPDF_ANNOT);
    } catch (...) {
        FPDFBitmap_Destroy(bitmap);
        throw;
    }

    // Destroy the tiny PDFium struct immediately — the pixel buffer itself
    // remains owned by the QImage and is not freed by FPDFBitmap_Destroy.
    FPDFBitmap_Destroy(bitmap);

    return image;
}

} // namespace pdfmark