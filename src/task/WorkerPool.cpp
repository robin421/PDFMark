// PDFMark - WorkerPool implementation.
#include "task/WorkerPool.h"
#include <algorithm>

namespace pdfmark {

int WorkerPool::idealWorkerCount(PerformanceMode mode) {
    // NOTE: PDFium is not thread-safe (see third_party/pdfium/include/fpdfview.h
    // and PdfLibrary::callMutex()); every PDFium call is serialised. Concurrency
    // therefore only overlaps the Qt-side work (watermark painting, JPEG
    // encoding, disk I/O) and bounds how many documents are open at once.
    int logicalCores = QThread::idealThreadCount();
    if (logicalCores <= 0) logicalCores = 4;

    switch (mode) {
        case PerformanceMode::Low:
            return 1;
        case PerformanceMode::Normal:
            // Two documents in flight: one holds the (serialised) PDFium lock
            // while the other does Qt-side work (watermark paint, JPEG encode).
            return 2;
        case PerformanceMode::High:
            // PDFium itself is serialised by PdfLibrary::callMutex(), so extra
            // workers only overlap the Qt-side work and each holds a whole
            // source + output document in RAM. 4 is the practical ceiling.
            return std::clamp(logicalCores / 2, 2, 4);
    }
    return 2;
}

int WorkerPool::idealWorkerCountFor(PerformanceMode mode, int memoryBudgetDocs) {
    const int cpuBased = idealWorkerCount(mode);
    if (memoryBudgetDocs <= 0) return cpuBased;
    return (std::max)(1, (std::min)(cpuBased, memoryBudgetDocs));
}

} // namespace pdfmark