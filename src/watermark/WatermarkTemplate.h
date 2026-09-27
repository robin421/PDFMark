// PDFMark - Reusable watermark template: multi-line text + one shared style.
#pragma once

#include "watermark/WatermarkConfig.h"
#include <string>
#include <vector>

namespace pdfmark {

// A named, reusable bundle of watermark settings. Every entry carries its own
// text plus the shared style fields (font, size, rotation, opacity, DPI,
// JPEG quality) and the per-entry `selected` flag used by "生成所选".
struct WatermarkTemplate {
    std::string name;
    std::vector<WatermarkConfig> watermarks;

    // A template is usable when it has a name and at least one non-empty text.
    bool isValid() const {
        if (name.empty()) return false;
        for (const auto& wm : watermarks) {
            if (!wm.text.empty()) return true;
        }
        return false;
    }

    // Style preview: first non-empty entry, or a default-constructed config.
    WatermarkConfig style() const {
        for (const auto& wm : watermarks) {
            if (!wm.text.empty()) return wm;
        }
        return WatermarkConfig{};
    }
};

// Expand a template into the per-file config list used by the batch pipeline.
// Entries with empty text are dropped so a template never produces a blank
// watermark.
inline std::vector<WatermarkConfig> templateToConfigs(const WatermarkTemplate& tpl) {
    std::vector<WatermarkConfig> out;
    out.reserve(tpl.watermarks.size());
    for (const auto& wm : tpl.watermarks) {
        if (wm.text.empty()) continue;
        out.push_back(wm);
    }
    return out;
}

} // namespace pdfmark
