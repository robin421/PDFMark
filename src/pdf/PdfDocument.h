// PDFMark - RAII wrapper for PDFium document and page operations.
#pragma once

#include "../common/Common.h"
#include <string>
#include <vector>

// Forward declarations for PDFium opaque types
using FPDF_DOCUMENT = struct fpdf_document_t__*;
using FPDF_PAGE = struct fpdf_page_t__*;

namespace pdfmark {

// Forward declarations of handles managed by RAII wrappers.
struct FpdfDocumentDeleter {
    void operator()(FPDF_DOCUMENT doc) const;
};
struct FpdfPageDeleter {
    void operator()(FPDF_PAGE page) const;
};

using PdfDocumentHandle = std::unique_ptr<std::remove_pointer_t<FPDF_DOCUMENT>, FpdfDocumentDeleter>;
using PdfPageHandle = std::unique_ptr<std::remove_pointer_t<FPDF_PAGE>, FpdfPageDeleter>;

class PdfDocument {
public:
    // Open existing document. Throws PdfError on failure.
    static PdfDocumentHandle open(const fs::path& path, const std::string& password = "");

    // Create a new (empty) document for writing.
    static PdfDocumentHandle create();

    // Get page count (const handle).
    static int pageCount(FPDF_DOCUMENT doc);

    // Load a single page (caller must manage lifecycle).
    // Returns unique handle. Throws PdfError if page index out of range.
    static PdfPageHandle loadPage(FPDF_DOCUMENT doc, int index);

    // Get page dimensions in PDF points (1/72 inch).
    static double getPageWidth(FPDF_PAGE page);
    static double getPageHeight(FPDF_PAGE page);

    // Save document to disk atomically.
    static void save(FPDF_DOCUMENT doc, const fs::path& path);

private:
    PdfDocument() = default;
};

} // namespace pdfmark