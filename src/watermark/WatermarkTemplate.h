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

// Template names are derived from the watermark text instead of being typed by
// the user: the first non-empty line, truncated to 6 characters with "...".
// Chinese characters count as one unit, so counting walks UTF-8 code points.
inline constexpr int kMaxTemplateNameChars = 6;

inline std::string templateNameFromConfigs(const std::vector<WatermarkConfig>& configs) {
    std::string text;
    for (const auto& cfg : configs) {
        if (cfg.text.empty()) continue;
        const size_t b = cfg.text.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) continue;
        const size_t e = cfg.text.find_last_not_of(" \t\r\n");
        text = cfg.text.substr(b, e - b + 1);
        if (!text.empty()) break;
    }
    if (text.empty()) return std::string();

    size_t chars = 0;
    size_t cut = text.size();
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        size_t len = (c < 0x80) ? 1u : (c >= 0xF0 ? 4u : (c >= 0xE0 ? 3u : 2u));
        if (i + len > text.size()) len = 1;
        if (chars == static_cast<size_t>(kMaxTemplateNameChars)) {
            cut = i;
            break;
        }
        ++chars;
        i += len;
    }
    if (cut < text.size()) return text.substr(0, cut) + "...";
    return text;
}

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
