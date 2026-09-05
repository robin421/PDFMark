// PDFMark - PdfDocument implementation.
#include "pdf/PdfDocument.h"
#include "pdf/PdfLibrary.h"
#include <fpdfview.h>
#include <fpdf_edit.h>
#include <fpdf_save.h>
#include <fpdf_doc.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace pdfmark {

// PDFium error code to human-readable string.
static std::string pdfiumError(unsigned long err) {
    switch (err) {
        case FPDF_ERR_SUCCESS: return "no error";
        case FPDF_ERR_UNKNOWN: return "unknown error";
        case FPDF_ERR_FILE: return "file not found or cannot be opened";
        case FPDF_ERR_FORMAT: return "file not in PDF format or corrupted";
        case FPDF_ERR_PASSWORD: return "password required or incorrect";
        case FPDF_ERR_SECURITY: return "unsupported security scheme";
        case FPDF_ERR_PAGE: return "page not found or content error";
        default: return "error code " + std::to_string(err);
    }
}

void FpdfDocumentDeleter::operator()(FPDF_DOCUMENT doc) const {
    if (doc) FPDF_CloseDocument(doc);
}

void FpdfPageDeleter::operator()(FPDF_PAGE page) const {
    if (page) FPDF_ClosePage(page);
}

PdfDocumentHandle PdfDocument::open(const fs::path& path, const std::string& password) {
    // Ensure library is initialized.
    PdfLibrary::initialize();

    std::string pathStr = pathToString(path);

    FPDF_DOCUMENT doc = FPDF_LoadDocument(pathStr.c_str(), password.empty() ? nullptr : password.c_str());
    if (!doc) {
        unsigned long err = FPDF_GetLastError();
        throw PdfError("Failed to open PDF: " + pathStr + " (" + pdfiumError(err) + ")");
    }

    return PdfDocumentHandle(doc);
}

PdfDocumentHandle PdfDocument::create() {
    PdfLibrary::initialize();

    FPDF_DOCUMENT doc = FPDF_CreateNewDocument();
    if (!doc) {
        throw PdfError("Failed to create new PDF document");
    }
    return PdfDocumentHandle(doc);
}

int PdfDocument::pageCount(FPDF_DOCUMENT doc) {
    if (!doc) return 0;
    return FPDF_GetPageCount(doc);
}

PdfPageHandle PdfDocument::loadPage(FPDF_DOCUMENT doc, int index) {
    if (!doc) throw PdfError("Null document");
    if (index < 0 || index >= FPDF_GetPageCount(doc)) {
        throw PdfError("Page index out of range: " + std::to_string(index));
    }
    FPDF_PAGE page = FPDF_LoadPage(doc, index);
    if (!page) {
        throw PdfError("Failed to load page index " + std::to_string(index));
    }
    return PdfPageHandle(page);
}

double PdfDocument::getPageWidth(FPDF_PAGE page) {
    if (!page) return 0.0;
    return FPDF_GetPageWidth(page);
}

double PdfDocument::getPageHeight(FPDF_PAGE page) {
    if (!page) return 0.0;
    return FPDF_GetPageHeight(page);
}

// Custom file write struct backed by std::ofstream.
struct FsFileWrite : public FPDF_FILEWRITE {
    std::FILE* file = nullptr;
    std::string path;
};

static int FsFileWrite_Block(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
    auto* fw = static_cast<FsFileWrite*>(self);
    if (!fw || !fw->file) return 0;
    if (size == 0) return 1;
    return std::fwrite(data, 1, size, fw->file) == size ? 1 : 0;
}

void PdfDocument::save(FPDF_DOCUMENT doc, const fs::path& path) {
    if (!doc) throw PdfError("Null document");

    FsFileWrite fw;
    fw.version = 1;
    fw.WriteBlock = FsFileWrite_Block;
    fw.path = pathToString(path);
    fw.file = std::fopen(fw.path.c_str(), "wb");
    if (!fw.file) {
        throw PdfError("Failed to open output file: " + fw.path);
    }

    FPDF_BOOL ok = FPDF_SaveAsCopy(doc, &fw, FPDF_NO_INCREMENTAL);

    std::fclose(fw.file);

    if (!ok) {
        unsigned long err = FPDF_GetLastError();
        // Clean up partial file
        std::error_code ec;
        fs::remove(path, ec);
        throw PdfError("Failed to save PDF: " + fw.path + " (" + pdfiumError(err) + ")");
    }
}

} // namespace pdfmark