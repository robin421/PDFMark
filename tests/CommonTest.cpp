#include "common/Common.h"
#include "diagnostics/Diagnostics.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

namespace pdfmark {

void testCommonUtilities() {
    std::cout << "[RUN] testCommonUtilities\n";

    // 1. DPI points to pixels
    // 72 points at 72 DPI == 72 pixels
    assert(pointsToPixels(72.0, 72) == 72);
    // 72 points at 200 DPI == 200 pixels
    assert(pointsToPixels(72.0, 200) == 200);
    // Standard A4 width: 595.276 points -> ~1654 px at 200 DPI
    int a4w = pointsToPixels(595.276, 200);
    assert(a4w >= 1650 && a4w <= 1655);

    // 2. Enum resolution
    assert(dpiFromResolution(RasterResolution::R150) == 150);
    assert(dpiFromResolution(RasterResolution::R200) == 200);
    assert(dpiFromResolution(RasterResolution::R300) == 300);

    // 3. ScopedTimer
    double elapsedMs = 0.0;
    {
        ScopedTimer timer(elapsedMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    assert(elapsedMs >= 15.0);

    // 4. Memory probe
    long long peakBytes = MemoryProbe::peakPhysicalBytes();
    assert(peakBytes >= 0);
    // 5. sanitizeFilename
    assert(sanitizeFilename("Confidential") == "Confidential");
    assert(sanitizeFilename("张三/人事*2026:机密") == "张三_人事_2026_机密");
    assert(sanitizeFilename("  test<file>?\"|  ") == "test_file");
    assert(sanitizeFilename("   ///:::***???   ") == "watermark");
    assert(sanitizeFilename("") == "watermark");

    std::cout << "[PASS] testCommonUtilities\n";
}

} // namespace pdfmark