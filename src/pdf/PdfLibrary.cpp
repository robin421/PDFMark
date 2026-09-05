// PDFMark - PDFium library global initialization implementation.
#include "pdf/PdfLibrary.h"
#include <fpdfview.h>
#include <iostream>

namespace pdfmark {

std::mutex PdfLibrary::s_mutex;
bool PdfLibrary::s_initialized = false;
int PdfLibrary::s_refCount = 0;

void PdfLibrary::initialize() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_refCount == 0 && !s_initialized) {
        FPDF_LIBRARY_CONFIG config;
        config.version = 2;
        config.m_pUserFontPaths = nullptr;
        config.m_pIsolate = nullptr;
        config.m_v8EmbedderSlot = 0;
        FPDF_InitLibraryWithConfig(&config);
        s_initialized = true;
    }
    s_refCount++;
}

void PdfLibrary::destroy() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_refCount > 0) {
        s_refCount--;
        if (s_refCount == 0 && s_initialized) {
            FPDF_DestroyLibrary();
            s_initialized = false;
        }
    }
}

bool PdfLibrary::isInitialized() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_initialized;
}

} // namespace pdfmark