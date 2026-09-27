// PDFMark - Pre-flight workload / memory budget estimation.
//
// The batch pipeline keeps, for every concurrently processed source document:
//   * the whole source PDF in memory (PdfDocument::open reads the file),
//   * the current page bitmap (plus one working copy per additional watermark),
//   * the *entire* output document (all rasterized pages) until it is saved.
//
// Nothing in the old code bounded that total, so a large "generate all" batch
// could exhaust RAM and be killed by the OS.  This header provides the pure,
// unit-testable arithmetic that turns a batch description into a peak-memory
// estimate and a safe concurrency recommendation.
#pragma once

#include <cstdint>
#include <vector>

namespace pdfmark {

// Per-document facts. 0 / defaults mean "unknown" and are handled conservatively.
struct WorkloadFileInfo {
    int pages = 0;            // 0 => unknown, falls back to kAssumedPagesPerDoc
    int watermarks = 1;       // watermark configs to generate for this file
    int64_t sourceBytes = 0;  // size of the source PDF on disk
    double widthPt = 595.0;   // page size used for the raster estimate (A4 default)
    double heightPt = 842.0;
};

struct WorkloadInput {
    std::vector<WorkloadFileInfo> files;
    int maxDpi = 200;             // highest DPI across the configured watermarks
    int64_t totalRamBytes = 0;    // 0 => unknown, a conservative fallback is used
    int64_t currentRssBytes = 0;  // memory already held by this process
    int cpuCap = 4;               // concurrency the current performance mode allows
};

enum class WorkloadRisk {
    Safe,     // even full CPU concurrency stays well inside the RAM budget
    Caution,  // full concurrency is close to the budget
    Risky,    // full concurrency exceeds the budget -> must reduce concurrency
};

struct WorkloadEstimate {
    int fileCount = 0;
    int watermarkCount = 0;     // total output documents (subtasks)
    int64_t estimatedPages = 0; // page rasterizations (= pages x watermarks)
    int64_t pageImageBytes = 0; // largest single rasterized page at maxDpi
    int64_t maxDocSourceBytes = 0; // largest source PDF in the batch
    int64_t perDocPeakBytes = 0;// worst-case footprint of one concurrent document
    int64_t peakBytes = 0;      // peak at recommendedConcurrentDocs
    int64_t peakAtCpuConcurrencyBytes = 0;
    int64_t usableRamBytes = 0;
    int recommendedConcurrentDocs = 1;
    WorkloadRisk risk = WorkloadRisk::Safe;
    bool pagesEstimated = false; // some page counts were unknown
};

// ── Tuning constants (exposed so tests can reason about them) ───────────────
inline constexpr int kAssumedPagesPerDoc = 10;               // when page count unknown
inline constexpr int64_t kOutputBytesPerPage = 400 * 1024;   // ~0.4 MB JPEG @200dpi/A4
inline constexpr double kUsableRamFraction = 0.5;            // never budget >50% of RAM
inline constexpr int64_t kRamReserveBytes = 512LL * 1024 * 1024;
inline constexpr int64_t kMinUsableRamBytes = 64LL * 1024 * 1024;

// UI pre-flight thresholds.
inline constexpr int kPreflightFileThreshold = 20;
inline constexpr int64_t kPreflightPageThreshold = 200;

WorkloadEstimate estimateWorkload(const WorkloadInput& in);

// ── Fully automatic concurrency ─────────────────────────────────────────────
// Derived from the machine (cores, RAM) and the workload (file count, page
// counts, PDF sizes). The user never chooses a "performance mode".
struct ConcurrencyPolicy {
    int maxConcurrentDocs = 1;   // sliding-window ceiling
    int poolThreads = 1;         // worker threads (equals the window; more is pointless)
    int throttleMs = 0;          // pause between submissions for very large batches
    int64_t ramBudgetBytes = 0;  // soft ceiling used for runtime adaptation
};

inline constexpr int kAutoThrottleMs = 30;

// Concurrency the CPU alone would justify. PDFium serialises page parsing and
// rasterisation (see PdfLibrary::callMutex()), so extra workers only overlap
// Qt-side JPEG encoding / disk IO while each holds a whole document in RAM.
inline int cpuBasedConcurrency(int logicalCores) {
    if (logicalCores <= 0) logicalCores = 4;
    int c = logicalCores / 2;
    if (c < 2) c = 2;
    if (c > 4) c = 4;
    return c;
}

// `logicalCores` is passed in so this header stays Qt-free.
ConcurrencyPolicy planConcurrency(const WorkloadEstimate& est, int logicalCores);

// True when the batch is large or memory-tight enough to warrant a dialog.
bool needsPreflightWarning(const WorkloadEstimate& e);

// Human readable risk label (UTF-8, Chinese UI).
const char* workloadRiskLabel(WorkloadRisk r);

// Bytes of a single rasterized page (w x h at `dpi`, 4 bytes/pixel).
int64_t pageRasterBytes(double widthPt, double heightPt, int dpi);

} // namespace pdfmark
