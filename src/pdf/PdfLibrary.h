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

    // Global lock that MUST be held around every single PDFium API call.
    //
    // third_party/pdfium/include/fpdfview.h states: "None of the PDFium APIs
    // are thread-safe. They expect to be called from a single thread. Barring
    // that, embedders are required to ensure (via a mutex or similar) that only
    // a single PDFium call can be made at a time."
    //
    // Recursive so nested calls (e.g. PdfRenderer -> PdfDocument::getPageWidth)
    // do not deadlock.
    static std::recursive_mutex& callMutex();

private:
    PdfLibrary() = default;
    ~PdfLibrary() = default;

    static std::mutex s_mutex;
    static bool s_initialized;
    static int s_refCount;
};

// RAII guard for PdfLibrary::callMutex(). Take this at the top of any function
// that calls into PDFium.
using PdfiumCallLock = std::lock_guard<std::recursive_mutex>;

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