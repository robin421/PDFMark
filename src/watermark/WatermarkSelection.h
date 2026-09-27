// PDFMark - Helpers for choosing which watermark configs join a batch.
#pragma once

#include "watermark/WatermarkConfig.h"
#include <vector>

namespace pdfmark {

// Subset of configs produced by "生成所选" (generate selected):
// text is non-empty AND the user kept the row checked.
//
// Kept as a small pure function so the selection rule can be unit-tested
// without instantiating the Qt UI.
inline std::vector<WatermarkConfig> filterSelectedWatermarks(
    const std::vector<WatermarkConfig>& configs) {
    std::vector<WatermarkConfig> out;
    out.reserve(configs.size());
    for (const auto& cfg : configs) {
        if (!cfg.selected) continue;
        if (cfg.text.empty()) continue;
        out.push_back(cfg);
    }
    return out;
}

} // namespace pdfmark
