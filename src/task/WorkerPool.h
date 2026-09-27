// PDFMark - WorkerPool: determines worker count from PerformanceMode.
#pragma once

#include "common/Common.h"
#include <QThread>

namespace pdfmark {

class WorkerPool {
public:
    static int idealWorkerCount(PerformanceMode mode);

    // CPU-based count additionally clamped by a RAM-derived budget. The batch
    // pipeline uses this so a large "generate all" never spawns more
    // concurrent documents than the machine can hold.
    static int idealWorkerCountFor(PerformanceMode mode, int memoryBudgetDocs);

    // NOTE: there is deliberately no helper that reconfigures
    // QThreadPool::globalInstance(). TaskManager owns its own pool so its
    // thread count cannot affect unrelated Qt users and can be drained.

private:
    WorkerPool() = default;
};

} // namespace pdfmark
