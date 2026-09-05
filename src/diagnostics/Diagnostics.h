// PDFMark - Diagnostics and performance metrics.
#pragma once

#include "common/Common.h"
#include <chrono>
#include <string>
#include <vector>

namespace pdfmark {

class ScopedTimer {
public:
    explicit ScopedTimer(double& outputMs)
        : outputMs_(outputMs),
          start_(std::chrono::steady_clock::now()) {}
    ~ScopedTimer() {
        auto end = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> ms = end - start_;
        outputMs_ = ms.count();
    }
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    double& outputMs_;
    std::chrono::steady_clock::time_point start_;
};

// Cross-platform physical memory of the current process (in bytes).
class MemoryProbe {
public:
    static long long peakPhysicalBytes();
    static long long currentPhysicalBytes();
};

struct DiagnosticsReport {
    std::string osName;
    int cpuLogicalCores = 0;
    long long totalRamBytes = 0;
    long long peakMemoryBytes = 0;
    double totalElapsedMs = 0.0;
    int totalPages = 0;
    int processedPages = 0;
    int failedPages = 0;
    double avgPageMs = 0.0;
    std::string inputPath;
    std::string outputPath;
    std::string watermarkText;
    std::string timestamp;

    // Writes a formatted, sanitized text report (no passwords, no PII).
    std::string toFormattedText() const;
};

// Returns a textual timestamp safe for inclusion in report filename.
std::string currentTimestampForFilename();

} // namespace pdfmark