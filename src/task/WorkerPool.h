// PDFMark - WorkerPool: determines worker count from PerformanceMode.
#pragma once

#include "common/Common.h"
#include <QThreadPool>
#include <QThread>

namespace pdfmark {

class WorkerPool {
public:
    static int idealWorkerCount(PerformanceMode mode);

    // Returns globally shared QThreadPool configured for the given mode.
    static QThreadPool* poolForMode(PerformanceMode mode);

private:
    WorkerPool() = default;
};

} // namespace pdfmark
