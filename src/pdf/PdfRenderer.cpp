// PDFMark - PdfRenderer implementation.
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

    // Guard bitmap creation in a nested try-catch
    FPDF_BITMAP bitmap = nullptr;
    try {
        bitmap = FPDFBitmap_Create(widthPx, heightPx, 0);
    } catch (...) {
        throw PdfError("FPDFBitmap_Create threw an exception");
    }

    if (!bitmap) {
        throw PdfError("Failed to allocate FPDF_BITMAP of size " +
                       std::to_string(widthPx) + "x" + std::to_string(heightPx));
    }

    try {
        // Fill background with white
        FPDFBitmap_FillRect(bitmap, 0, 0, widthPx, heightPx, 0xFFFFFFFF);

        // Render page — this is where pdfium crashes on malformed XFA/forms
        FPDF_RenderPageBitmap(bitmap, page, 0, 0, widthPx, heightPx, 0, FPDF_ANNOT);

        // Read pixel buffer
        void* buffer = FPDFBitmap_GetBuffer(bitmap);
        int stride = FPDFBitmap_GetStride(bitmap);

        if (!buffer || stride <= 0) {
            throw PdfError("FPDFBitmap_GetBuffer returned null or invalid stride");
        }

        // Hand over ownership of FPDF_BITMAP buffer directly to QImage without deep copying.
        // The cleanup callback automatically destroys the FPDF_BITMAP when QImage goes out of scope.
        auto cleanup = [](void* info) {
            if (info) {
                FPDFBitmap_Destroy(static_cast<FPDF_BITMAP>(info));
            }
        };

        uchar* ubuf = static_cast<uchar*>(buffer);
        QImage result(ubuf, widthPx, heightPx, stride, QImage::Format_RGB32, cleanup, bitmap);

        if (result.isNull()) {
            FPDFBitmap_Destroy(bitmap);
            throw PdfError("Failed to wrap FPDF_BITMAP into QImage");
        }

        return result;
    } catch (...) {
        FPDFBitmap_Destroy(bitmap);
        throw;
    }
}

} // namespace pdfmark