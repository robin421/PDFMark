// PDFMark - PDFium library global initialization and teardown RAII.
#pragma once

#include <mutex>

namespace pdfmark {

class PdfLibrary {
public:
    // Thread-safe library initialization. Safe to call multiple times.
    static void initialize();
    
    // Tear down library. Typically called on process exit.
    static void destroy();

    // Check if initialized
    static bool isInitialized();

private:
    PdfLibrary() = default;
    ~PdfLibrary() = default;

    static std::mutex s_mutex;
    static bool s_initialized;
    static int s_refCount;
};

// Scoped helper that initializes in constructor and releases in destructor.
class ScopedPdfLibrary {
public:
    ScopedPdfLibrary() {
        PdfLibrary::initialize();
    }
    ~ScopedPdfLibrary() {
        PdfLibrary::destroy();
    }

    ScopedPdfLibrary(const ScopedPdfLibrary&) = delete;
    ScopedPdfLibrary& operator=(const ScopedPdfLibrary&) = delete;
};

} // namespace pdfmark