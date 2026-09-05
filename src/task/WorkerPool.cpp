// PDFMark - WorkerPool implementation.
#include "task/WorkerPool.h"
#include <algorithm>

namespace pdfmark {

int WorkerPool::idealWorkerCount(PerformanceMode mode) {
    int logicalCores = QThread::idealThreadCount();
    if (logicalCores <= 0) logicalCores = 4;

    switch (mode) {
        case PerformanceMode::Low:
            return 1;
        case PerformanceMode::Normal:
            // Standard balance: 2 to 4 workers, leaves cores free for UI and disk I/O
            return std::clamp(logicalCores / 2, 2, 4);
        case PerformanceMode::High:
            // High throughput: leave 1 core for OS/UI, max 8 to prevent RAM exhaustion
            return std::clamp(logicalCores - 1, 2, 8);
    }
    return 2;
}

QThreadPool* WorkerPool::poolForMode(PerformanceMode mode) {
    QThreadPool* pool = QThreadPool::globalInstance();
    pool->setMaxThreadCount(idealWorkerCount(mode));
    return pool;
}

} // namespace pdfmark