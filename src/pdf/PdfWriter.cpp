// PDFMark - PdfWriter implementation: inserts full-page raster images as JPEG.
#include "pdf/PdfWriter.h"
#include "pdf/PdfDocument.h"
#include "common/Common.h"
#include <fpdfview.h>
#include <fpdf_edit.h>
#include <QBuffer>
#include <QImageWriter>
#include <QDebug>
#include <cstring>

namespace pdfmark {

// FPDF_FILEACCESS adapter over QByteArray for FPDFImageObj_LoadJpegFileInline.
static int JpegGetBlockImpl(void* param,
                            unsigned long position,
                            unsigned char* pBuf,
                            unsigned long size) {
    const QByteArray* data = static_cast<const QByteArray*>(param);
    if (!data || !pBuf) return 0;
    if (position + size > static_cast<unsigned long>(data->size())) return 0;
    std::memcpy(pBuf, data->constData() + position, size);
    return 1;
}

void PdfWriter::appendRasterPage(FPDF_DOCUMENT doc,
                                 const QImage& image,
                                 double widthPt,
                                 double heightPt,
                                 int jpegQuality) {
    if (!doc) throw PdfError("Null output document");
    if (image.isNull()) throw PdfError("Null image");
    if (widthPt <= 0 || heightPt <= 0) throw PdfError("Invalid page dimensions");
    if (jpegQuality < 10 || jpegQuality > 100) jpegQuality = 85;

    int pageIndex = FPDF_GetPageCount(doc);
    FPDF_PAGE page = FPDFPage_New(doc, pageIndex, widthPt, heightPt);
    if (!page) {
        throw PdfError("Failed to create new page at index " + std::to_string(pageIndex));
    }

    // JPEG compress the image in memory
    QByteArray jpegData;
    qsizetype estimatedBytes = (static_cast<qsizetype>(image.width()) * image.height() * 4) / 4;
    if (estimatedBytes > 0) {
        jpegData.reserve(estimatedBytes);
    }
    QBuffer buffer(&jpegData);
    if (!buffer.open(QIODevice::WriteOnly)) {
        FPDF_ClosePage(page);
        throw PdfError("Failed to open QBuffer for JPEG encoding");
    }

    // Use QImage::save for simplicity (wraps QImageWriter internally)
    if (!image.save(&buffer, "JPEG", jpegQuality)) {
        FPDF_ClosePage(page);
        throw PdfError("JPEG compression failed for page " + std::to_string(pageIndex));
    }
    buffer.close();

    if (jpegData.isEmpty()) {
        FPDF_ClosePage(page);
        throw PdfError("JPEG output empty for page " + std::to_string(pageIndex));
    }

    FPDF_PAGEOBJECT imgObj = FPDFPageObj_NewImageObj(doc);
    if (!imgObj) {
        FPDF_ClosePage(page);
        throw PdfError("Failed to create image object");
    }

    FPDF_FILEACCESS fileAccess{};
    fileAccess.m_FileLen = static_cast<unsigned long>(jpegData.size());
    fileAccess.m_GetBlock = JpegGetBlockImpl;
    fileAccess.m_Param = static_cast<void*>(&jpegData);

    // Inline: copies JPEG content into PDF stream; fileAccess may be destroyed after call.
    FPDF_BOOL ok = FPDFImageObj_LoadJpegFileInline(nullptr, 0, imgObj, &fileAccess);
    if (!ok) {
        FPDFPageObj_Destroy(imgObj);
        FPDF_ClosePage(page);
        throw PdfError("Failed to load JPEG into image object for page " + std::to_string(pageIndex));
    }

    // Scale 1x1 image object to full page dimensions using transform matrix:
    // | widthPt  0       0 |
    // | 0        heightPt 0 |
    if (!FPDFImageObj_SetMatrix(imgObj, widthPt, 0.0, 0.0, heightPt, 0.0, 0.0)) {
        FPDF_ClosePage(page);
        throw PdfError("Failed to set image matrix");
    }

    FPDFPage_InsertObject(page, imgObj);

    if (!FPDFPage_GenerateContent(page)) {
        FPDF_ClosePage(page);
        throw PdfError("Failed to generate page content");
    }

    FPDF_ClosePage(page);
}

} // namespace pdfmark
