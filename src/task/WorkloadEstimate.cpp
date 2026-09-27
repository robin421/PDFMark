// PDFMark - Pre-flight workload / memory budget estimation.
#include "task/WorkloadEstimate.h"

#include <algorithm>
#include <cmath>

namespace pdfmark {

int64_t pageRasterBytes(double widthPt, double heightPt, int dpi) {
    const double w = (std::max)(1.0, widthPt) * dpi / 72.0;
    const double h = (std::max)(1.0, heightPt) * dpi / 72.0;
    const int64_t wp = static_cast<int64_t>(std::llround(w));
    const int64_t hp = static_cast<int64_t>(std::llround(h));
    return wp * hp * 4; // QImage::Format_RGB32
}

WorkloadEstimate estimateWorkload(const WorkloadInput& in) {
    WorkloadEstimate e;
    e.fileCount = static_cast<int>(in.files.size());
    e.pageImageBytes = pageRasterBytes(595.0, 842.0, in.maxDpi); // A4 baseline

    if (in.files.empty()) {
        e.recommendedConcurrentDocs = 1;
        e.risk = WorkloadRisk::Safe;
        return e;
    }

    int64_t maxDocSource = 0;
    int64_t maxDocPageUnits = 0; // pages * watermarks of the heaviest document
    bool unknownPages = false;

    for (const auto& f : in.files) {
        const int wm = (std::max)(1, f.watermarks);
        int pages = f.pages;
        if (pages <= 0) {
            pages = kAssumedPagesPerDoc;
            unknownPages = true;
        }

        e.watermarkCount += wm;
        const int64_t units = static_cast<int64_t>(pages) * wm;
        e.estimatedPages += units;

        maxDocSource = (std::max)(maxDocSource, f.sourceBytes);
        maxDocPageUnits = (std::max)(maxDocPageUnits, units);
        e.pageImageBytes = (std::max)(e.pageImageBytes, pageRasterBytes(f.widthPt, f.heightPt, in.maxDpi));
    }
    e.pagesEstimated = unknownPages;

    // Worst-case concurrent document: source bytes + base page image + one
    // working copy (transient, per additional watermark) + the full output
    // document buffered in memory until save().
    e.maxDocSourceBytes = maxDocSource;
    e.perDocPeakBytes = maxDocSource
                      + 2 * e.pageImageBytes
                      + maxDocPageUnits * kOutputBytesPerPage;

    // Usable RAM budget: at most half of system RAM, minus what we already hold
    // and a fixed reserve for the OS, the UI and Qt itself.
    int64_t usable;
    if (in.totalRamBytes > 0) {
        usable = static_cast<int64_t>(static_cast<double>(in.totalRamBytes) * kUsableRamFraction);
    } else {
        // Unknown total RAM: assume a modest 4 GB machine.
        usable = 2LL * 1024 * 1024 * 1024;
    }
    usable -= (std::max)(int64_t{0}, in.currentRssBytes);
    usable -= kRamReserveBytes;
    if (usable < kMinUsableRamBytes) usable = kMinUsableRamBytes;
    e.usableRamBytes = usable;

    const int cpuCap = (std::max)(1, in.cpuCap);
    int64_t byRam = e.perDocPeakBytes > 0 ? usable / e.perDocPeakBytes : cpuCap;
    if (byRam < 1) byRam = 1;
    e.recommendedConcurrentDocs = static_cast<int>((std::min)(byRam, static_cast<int64_t>(cpuCap)));

    e.peakBytes = e.perDocPeakBytes * e.recommendedConcurrentDocs;
    e.peakAtCpuConcurrencyBytes = e.perDocPeakBytes * cpuCap;

    if (e.peakAtCpuConcurrencyBytes <= static_cast<int64_t>(static_cast<double>(usable) * 0.7)) {
        e.risk = WorkloadRisk::Safe;
    } else if (e.peakAtCpuConcurrencyBytes <= usable) {
        e.risk = WorkloadRisk::Caution;
    } else {
        e.risk = WorkloadRisk::Risky;
    }
    return e;
}

ConcurrencyPolicy planConcurrency(const WorkloadEstimate& est, int logicalCores) {
    ConcurrencyPolicy policy;
    const int cpuCap = cpuBasedConcurrency(logicalCores);

    int docs = est.recommendedConcurrentDocs;
    if (docs < 1) docs = 1;
    if (docs > cpuCap) docs = cpuCap;

    policy.maxConcurrentDocs = docs;
    // The window is the binding constraint, so the pool needs exactly that many
    // threads; anything more would just hold extra documents in RAM.
    policy.poolThreads = docs;
    policy.ramBudgetBytes = est.usableRamBytes;

    // Very large queues get a small pause between submissions so the machine
    // (and the UI) stays responsive while working through them.
    policy.throttleMs = est.fileCount > 4 * docs ? kAutoThrottleMs : 0;
    return policy;
}

bool needsPreflightWarning(const WorkloadEstimate& e) {
    if (e.fileCount > kPreflightFileThreshold) return true;
    if (e.estimatedPages > kPreflightPageThreshold) return true;
    if (e.risk != WorkloadRisk::Safe) return true;
    return false;
}

const char* workloadRiskLabel(WorkloadRisk r) {
    switch (r) {
        case WorkloadRisk::Safe:    return "安全";
        case WorkloadRisk::Caution: return "注意";
        case WorkloadRisk::Risky:   return "风险";
    }
    return "未知";
}

} // namespace pdfmark
