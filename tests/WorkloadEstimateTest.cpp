// PDFMark - Unit tests for the pre-flight workload / memory budget estimator.
#include "task/WorkloadEstimate.h"

#include <cassert>
#include <iostream>

namespace pdfmark {

namespace {

WorkloadFileInfo doc(int pages, int watermarks, int64_t sourceBytes) {
    WorkloadFileInfo f;
    f.pages = pages;
    f.watermarks = watermarks;
    f.sourceBytes = sourceBytes;
    f.widthPt = 595.0;
    f.heightPt = 842.0;
    return f;
}

constexpr int64_t kGb = 1024LL * 1024 * 1024;
constexpr int64_t kMb = 1024LL * 1024;

} // namespace

void testWorkloadEstimate() {
    std::cout << "[RUN] testWorkloadEstimate\n";

    // ── empty batch ───────────────────────────────────────────────────────
    {
        WorkloadEstimate e = estimateWorkload(WorkloadInput{});
        assert(e.fileCount == 0);
        assert(e.watermarkCount == 0);
        assert(e.recommendedConcurrentDocs == 1);
        assert(e.risk == WorkloadRisk::Safe);
        assert(!needsPreflightWarning(e));
    }

    // ── one small document on a roomy machine: full CPU concurrency is safe ─
    {
        WorkloadInput in;
        in.files.push_back(doc(10, 1, 1 * kMb));
        in.maxDpi = 200;
        in.totalRamBytes = 8 * kGb;
        in.currentRssBytes = 100 * kMb;
        in.cpuCap = 4;

        WorkloadEstimate e = estimateWorkload(in);
        assert(e.fileCount == 1);
        assert(e.watermarkCount == 1);
        assert(e.estimatedPages == 10);
        assert(!e.pagesEstimated);
        assert(e.recommendedConcurrentDocs == 4);   // clamped by cpuCap
        assert(e.risk == WorkloadRisk::Safe);
        assert(!needsPreflightWarning(e));
        assert(e.pageImageBytes > 14 * kMb && e.pageImageBytes < 17 * kMb); // A4@200dpi ~15.5MB
    }

    // ── heavy batch: many pages x watermarks => Risky, concurrency reduced ──
    {
        WorkloadInput in;
        in.files.push_back(doc(500, 1, 50 * kMb));
        in.files.push_back(doc(500, 1, 50 * kMb));
        in.files.push_back(doc(500, 1, 50 * kMb)); // 3 watermarks in total
        in.files[0].watermarks = 3;
        in.maxDpi = 200;
        in.totalRamBytes = 4 * kGb;
        in.currentRssBytes = 100 * kMb;
        in.cpuCap = 8;

        WorkloadEstimate e = estimateWorkload(in);
        assert(e.watermarkCount == 5);
        assert(e.estimatedPages == 500 * 3 + 500 + 500);
        // 50MB source + 2*15.5MB images + 1500*0.4MB output ~= 681MB
        assert(e.perDocPeakBytes > 600 * kMb && e.perDocPeakBytes < 800 * kMb);
        assert(e.recommendedConcurrentDocs >= 1 && e.recommendedConcurrentDocs < 8);
        assert(e.risk == WorkloadRisk::Risky);
        assert(needsPreflightWarning(e));
        assert(e.peakBytes <= e.peakAtCpuConcurrencyBytes);
    }

    // ── unknown page counts are estimated, never treated as zero ───────────
    {
        WorkloadInput in;
        in.files.push_back(doc(0, 2, 1 * kMb));
        in.totalRamBytes = 8 * kGb;
        in.cpuCap = 4;

        WorkloadEstimate e = estimateWorkload(in);
        assert(e.pagesEstimated);
        assert(e.estimatedPages == static_cast<int64_t>(kAssumedPagesPerDoc) * 2);
    }

    // ── tiny RAM budget must never yield zero concurrency ─────────────────
    {
        WorkloadInput in;
        in.files.push_back(doc(1000, 4, 400 * kMb));
        in.totalRamBytes = 1 * kGb;
        in.currentRssBytes = 900 * kMb;
        in.cpuCap = 8;

        WorkloadEstimate e = estimateWorkload(in);
        assert(e.recommendedConcurrentDocs == 1);
        assert(e.usableRamBytes >= kMinUsableRamBytes);
        assert(e.risk == WorkloadRisk::Risky);
    }

    // ── warning thresholds ────────────────────────────────────────────────
    {
        WorkloadEstimate e;
        e.fileCount = kPreflightFileThreshold;      // exactly at threshold
        e.estimatedPages = 10;
        e.risk = WorkloadRisk::Safe;
        assert(!needsPreflightWarning(e));

        e.fileCount = kPreflightFileThreshold + 1;
        assert(needsPreflightWarning(e));

        e.fileCount = 1;
        e.estimatedPages = kPreflightPageThreshold + 1;
        assert(needsPreflightWarning(e));

        e.estimatedPages = 10;
        e.risk = WorkloadRisk::Caution;
        assert(needsPreflightWarning(e));
    }

    // ── raster byte helper sanity ─────────────────────────────────────────
    {
        const int64_t a4_200 = pageRasterBytes(595.0, 842.0, 200);
        const int64_t a4_300 = pageRasterBytes(595.0, 842.0, 300);
        assert(a4_200 > 14 * kMb && a4_200 < 17 * kMb);
        assert(a4_300 > a4_200);   // higher DPI => larger raster
    }

    // ── automatic concurrency policy ──────────────────────────────────────
    {
        // CPU-only ceiling: PDFium serialises rasterisation, so never more
        // than 4 workers even on a 64-core machine.
        assert(cpuBasedConcurrency(0) == 2);
        assert(cpuBasedConcurrency(2) == 2);
        assert(cpuBasedConcurrency(4) == 2);
        assert(cpuBasedConcurrency(8) == 4);
        assert(cpuBasedConcurrency(64) == 4);
    }
    {
        // Empty batch: exactly one document, no throttle.
        WorkloadInput in;
        in.totalRamBytes = 8 * kGb;
        in.cpuCap = 4;
        ConcurrencyPolicy p = planConcurrency(estimateWorkload(in), 8);
        assert(p.maxConcurrentDocs == 1);
        assert(p.poolThreads == 1);
        assert(p.throttleMs == 0);
    }
    {
        // Roomy machine + small files => CPU-capped, and the pool gets exactly
        // as many threads as the window (more would only hold extra documents).
        WorkloadInput in;
        for (int i = 0; i < 3; ++i) in.files.push_back(doc(10, 1, 1 * kMb));
        in.totalRamBytes = 32 * kGb;
        in.currentRssBytes = 100 * kMb;
        in.cpuCap = 4;

        WorkloadEstimate e = estimateWorkload(in);
        ConcurrencyPolicy p = planConcurrency(e, 8);
        assert(p.maxConcurrentDocs == 4);
        assert(p.poolThreads == p.maxConcurrentDocs);
        assert(p.throttleMs == 0);
        assert(p.ramBudgetBytes == e.usableRamBytes);
        assert(e.maxDocSourceBytes == 1 * kMb);
    }
    {
        // Huge scanned PDF on a modest machine => RAM forces 1, well below the
        // CPU-based ceiling.
        WorkloadInput in;
        in.files.push_back(doc(800, 3, 300 * kMb));
        in.totalRamBytes = 4 * kGb;
        in.currentRssBytes = 200 * kMb;
        in.cpuCap = 4;

        WorkloadEstimate e = estimateWorkload(in);
        ConcurrencyPolicy p = planConcurrency(e, 16);
        assert(p.maxConcurrentDocs == 1);
        assert(p.maxConcurrentDocs <= cpuBasedConcurrency(16));
        assert(p.throttleMs == 0);   // 1 file, so nothing to pace
    }
    {
        // Large queue => a small pause between submissions, and the window is
        // never widened by having many files.
        WorkloadInput in;
        for (int i = 0; i < 40; ++i) in.files.push_back(doc(5, 1, 1 * kMb));
        in.totalRamBytes = 8 * kGb;
        in.cpuCap = 4;

        WorkloadEstimate e = estimateWorkload(in);
        ConcurrencyPolicy p = planConcurrency(e, 4);
        assert(p.maxConcurrentDocs >= 1 && p.maxConcurrentDocs <= 2);
        assert(p.throttleMs == kAutoThrottleMs);
    }

    std::cout << "[PASS] testWorkloadEstimate\n";
}

} // namespace pdfmark
