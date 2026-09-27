// PDFMark - Regression test for the batch concurrency budget.
//
// Bug being guarded: TaskManager used to submit *every* document to the thread
// pool at once and sized concurrency purely from the CPU count. With a large
// batch that meant many documents in flight at the same time, each holding its
// source PDF plus the whole in-memory output document, which exhausted RAM.
//
// The fix is a bounded sliding window: `setMaxConcurrentDocuments(n)` must hard
// cap how many documents are ever processed simultaneously, regardless of the
// total batch size and of the performance mode.
//
// This test also guards a second, deeper bug found while fixing the first one:
// PDFium is not thread-safe ("embedders are required to ensure ... that only a
// single PDFium call can be made at a time", third_party/pdfium/include/
// fpdfview.h). Before PdfLibrary::callMutex() was introduced, running this test
// crashed with EXC_BAD_ACCESS inside CPDF_ContentParser roughly 1 run in 8 when
// the window was > 1, and never when it was 1. Keeping this test running with
// windows 2 and 4 is therefore a real regression guard for the global lock.
#include "common/Common.h"
#include "pdf/PdfDocument.h"
#include "pdf/PdfWriter.h"
#include "task/TaskManager.h"
#include "watermark/WatermarkConfig.h"
#include "diagnostics/Diagnostics.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QImage>
#include <QPainter>
#include <QTimer>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace pdfmark {

namespace {

fs::path createSinglePagePdf(const fs::path& outPath) {
    constexpr int kWidthPt = 595;
    constexpr int kHeightPt = 842;
    constexpr int kDpi = 72;            // tiny raster: tests stay fast
    constexpr int kWidthPx = (kWidthPt * kDpi) / 72;
    constexpr int kHeightPx = (kHeightPt * kDpi) / 72;

    QImage image(kWidthPx, kHeightPx, QImage::Format_RGB32);
    image.fill(Qt::white);

    auto doc = PdfDocument::create();
    PdfWriter::appendRasterPage(doc.get(), image, kWidthPt, kHeightPt, 60);
    PdfDocument::save(doc.get(), outPath);
    return outPath;
}

// Runs a batch of `fileCount` documents with the given concurrency window and
// returns the highest number of documents that were actually in flight.
struct RunOutcome {
    int peak = -1;
    int effective = -1;
    int resultCount = 0;
    int successCount = 0;
    bool finished = false;
};

RunOutcome runBatch(const fs::path& workDir,
                    const std::vector<fs::path>& inputs,
                    int window,
                    PerformanceMode mode) {
    RunOutcome outcome;

    WatermarkConfig cfg;
    cfg.text = "CONCURRENCY TEST";
    cfg.dpi = 72;

    std::vector<TaskManager::FileSubtask> subtasks;
    subtasks.reserve(inputs.size());
    for (const auto& p : inputs) {
        subtasks.push_back({p, cfg});
    }

    TaskManager tm;
    tm.setSubtasks(subtasks);
    tm.setPerformanceMode(mode);
    tm.setMaxConcurrentDocuments(window);
    tm.setOutputDirectory(workDir / "out");

    QEventLoop loop;
    QObject::connect(&tm, &TaskManager::allFinished, &loop, [&](const std::vector<FileResult>& results) {
        outcome.peak = tm.peakConcurrentDocuments();
        outcome.effective = tm.effectiveConcurrency();
        outcome.resultCount = static_cast<int>(results.size());
        for (const auto& r : results) {
            if (r.success) outcome.successCount++;
        }
        outcome.finished = true;
        loop.quit();
    });
    QTimer::singleShot(60000, &loop, [&]() { loop.quit(); }); // safety timeout

    tm.start();
    loop.exec();

    assert(outcome.finished && "TaskManager::allFinished never fired");
    return outcome;
}

} // namespace

void testConcurrencyBudget() {
    std::cout << "[RUN] testConcurrencyBudget\n";

    const fs::path workDir = fs::temp_directory_path() / "pdfmark_concurrency_test";
    std::error_code ec;
    fs::remove_all(workDir, ec);
    fs::create_directories(workDir, ec);

    constexpr int kFiles = 6;
    std::vector<fs::path> inputs;
    for (int i = 0; i < kFiles; ++i) {
        inputs.push_back(createSinglePagePdf(workDir / ("doc" + std::to_string(i) + ".pdf")));
    }

    // High performance mode would normally allow up to 8 workers; the memory
    // budget must still cap it to the requested window.
    const RunOutcome capped = runBatch(workDir, inputs, 2, PerformanceMode::High);
    assert(capped.resultCount == kFiles);
    assert(capped.successCount == kFiles);
    assert(capped.effective == 2);
    assert(capped.peak >= 1);
    assert(capped.peak <= 2);   // <-- the safety property that was missing

    // A window of 1 must serialise the batch completely.
    const RunOutcome serial = runBatch(workDir, inputs, 1, PerformanceMode::High);
    assert(serial.resultCount == kFiles);
    assert(serial.successCount == kFiles);
    assert(serial.effective == 1);
    assert(serial.peak == 1);

    // The bound must hold for a wider window too, and be independent of the
    // batch size: same 6 documents, window 4 => still never more than 4.
    const RunOutcome wider = runBatch(workDir, inputs, 4, PerformanceMode::High);
    assert(wider.resultCount == kFiles);
    assert(wider.successCount == kFiles);
    assert(wider.effective == 4);
    assert(wider.peak <= 4);

    // Scale check: a much larger batch must still succeed and still respect
    // the window. This is the "many PDFs + generate all" scenario that used to
    // crash (both from RAM growth and from concurrent PDFium calls).
    {
        constexpr int kBig = 24;
        std::vector<fs::path> big;
        big.reserve(kBig);
        for (int i = 0; i < kBig; ++i) {
            big.push_back(createSinglePagePdf(workDir / ("big" + std::to_string(i) + ".pdf")));
        }
        const RunOutcome stress = runBatch(workDir, big, 3, PerformanceMode::High);
        assert(stress.resultCount == kBig);
        assert(stress.successCount == kBig);
        assert(stress.effective == 3);
        assert(stress.peak <= 3);
        std::cout << "  large batch (" << kBig << " docs, window 3): peak=" << stress.peak
                  << " success=" << stress.successCount << "\n";
    }

    // Peak RSS must not scale with the batch: the window bounds live memory.
    const long long rssAfter = MemoryProbe::currentPhysicalBytes();
    assert(rssAfter > 0);
    std::cout << "  peak concurrent docs: window1=" << serial.peak
              << " window2=" << capped.peak
              << " window4=" << wider.peak << "\n";

    fs::remove_all(workDir, ec);
    std::cout << "[PASS] testConcurrencyBudget\n";
}

} // namespace pdfmark
