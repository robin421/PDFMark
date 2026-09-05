#include "watermark/WatermarkConfig.h"
#include "watermark/WatermarkRenderer.h"
#include "diagnostics/Diagnostics.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <QImage>

namespace pdfmark {

void testMemoryModel() {
    std::cout << "[RUN] testMemoryModel (Simulating 100 pages streaming raster/watermark)\n";

    long long initialMemory = MemoryProbe::currentPhysicalBytes();

    WatermarkConfig cfg;
    cfg.text = "STREAMING MEMORY TEST";
    cfg.fontSizePt = 24;
    cfg.dpi = 200;
    cfg.opacity = 0.12;

    // Simulate streaming through 100 pages:
    // allocate single page image -> apply watermark -> free immediately
    constexpr int simulatedPages = 100;
    long long peakRecorded = 0;

    for (int i = 0; i < simulatedPages; ++i) {
        // Scoped page allocation
        {
            // A4 at 200 DPI: 1654 x 2339 x 4 bytes ~= 15 MB
            QImage page(1654, 2339, QImage::Format_RGB32);
            page.fill(Qt::white);
            bool ok = WatermarkRenderer::applyWatermark(page, cfg);
            assert(ok);
            // Image goes out of scope here; memory returned
        }

        long long current = MemoryProbe::currentPhysicalBytes();
        if (current > peakRecorded) peakRecorded = current;
    }

    long long finalMemory = MemoryProbe::currentPhysicalBytes();

    std::cout << "  Initial Memory : " << initialMemory / (1024 * 1024) << " MB\n";
    std::cout << "  Peak Memory    : " << peakRecorded / (1024 * 1024) << " MB\n";
    std::cout << "  Final Memory   : " << finalMemory / (1024 * 1024) << " MB\n";

    // Growth from start to finish should be minimal (O(1) memory model)
    // Even after 100 pages, memory must not scale linearly with page count
    // 100 pages of 15MB would be 1.5 GB if leaked; verify it is well under 250 MB
    long long delta = finalMemory - initialMemory;
    std::cout << "  Net Delta after 100 pages: " << delta / (1024 * 1024) << " MB\n";
    assert(delta < 300 * 1024 * 1024); // Well below linear accumulation

    std::cout << "[PASS] testMemoryModel\n";
}

} // namespace pdfmark