// PDFMark - PdfRenderer implementation.
#include "pdf/PdfRenderer.h"
#include "pdf/PdfDocument.h"
#include "pdf/PdfLibrary.h"
#include "common/Common.h"

#include <fpdfview.h>
#include <cmath>

namespace pdfmark {

QImage PdfRenderer::rasterizePage(FPDF_PAGE page, int dpi) {
    if (!page) throw PdfError("Null page handle");
    if (dpi < 72 || dpi > 600) {
        throw PdfError("DPI out of range (72-600): " + std::to_string(dpi));
    }

    // Page dimensions in PDF points (1/72 inch)
    double widthPt = PdfDocument::getPageWidth(page);
    double heightPt = PdfDocument::getPageHeight(page);
    if (widthPt <= 0.0 || heightPt <= 0.0) {
        throw PdfError("Invalid page dimensions");
    }

    int widthPx = std::max(1, static_cast<int>(std::round(widthPt * dpi / 72.0)));
    int heightPx = std::max(1, static_cast<int>(std::round(heightPt * dpi / 72.0)));

    // Create 32-bit BGRx bitmap (alpha = 0: no alpha channel, 4 bytes per pixel)
    FPDF_BITMAP bitmap = FPDFBitmap_Create(widthPx, heightPx, 0);
    if (!bitmap) {
        throw PdfError("Failed to allocate FPDF_BITMAP of size " +
                       std::to_string(widthPx) + "x" + std::to_string(heightPx));
    }

    // Fill background with white (0xFFFFFFFF)
    FPDFBitmap_FillRect(bitmap, 0, 0, widthPx, heightPx, 0xFFFFFFFF);

    // Render page content onto bitmap with annotations enabled
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, widthPx, heightPx, 0, FPDF_ANNOT);

    // Read pixel buffer
    void* buffer = FPDFBitmap_GetBuffer(bitmap);
    int stride = FPDFBitmap_GetStride(bitmap);

    if (!buffer || stride <= 0) {
        FPDFBitmap_Destroy(bitmap);
        throw PdfError("Failed to read rendered bitmap buffer");
    }

    // QImage::Format_RGB32 interprets 0x00RRGGBB in native-endian (little endian = B, G, R, 0)
    // Matches PDFium's BGRx layout on both Windows and macOS (little-endian ARM/x86).
    const uchar* ubuf = static_cast<const uchar*>(buffer);
    QImage result = QImage(ubuf, widthPx, heightPx, stride, QImage::Format_RGB32).copy();

    // Free PDFium bitmap immediately to maintain O(1) memory
    FPDFBitmap_Destroy(bitmap);

    return result;
}

} // namespace pdfmark